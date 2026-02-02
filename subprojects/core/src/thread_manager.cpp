#include "thread_manager.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <print>
#include <stdexcept>

namespace core {

#ifdef ENGINE_DEBUG
// In debug mode, use a dynamically allocated singleton for hot reload support
// The singleton can be swapped via setInstance() during hot reload.
// Thread safety: call_once ensures thread-safe initialization.
// The hot reload system coordinates access - it stops all activity before
// swapping instances, so we don't need mutex protection on every access.
static ThreadManager *g_threadManagerInstance = nullptr;
static std::once_flag g_threadManagerOnce;

ThreadManager &ThreadManager::instance() {
  // Use call_once for thread-safe initial creation
  std::call_once(g_threadManagerOnce, []() {
    g_threadManagerInstance = new ThreadManager();
  });
  return *g_threadManagerInstance;
}

void ThreadManager::setInstance(ThreadManager *inst) {
  // Called by hot reload system when coordinating instance swap
  // The hot reload system ensures no threads are using the old instance
  g_threadManagerInstance = inst;
}

ThreadManager *ThreadManager::getInstance() {
  return g_threadManagerInstance;
}

#else
// In release mode, use classic static local variable singleton
ThreadManager &ThreadManager::instance() {
  static ThreadManager instance;
  return instance;
}
#endif

ThreadManager::ThreadManager() = default;

ThreadManager::~ThreadManager() { shutdown(); }

void ThreadManager::shutdown() {
  std::lock_guard<std::mutex> lock(threadMutex_);
  for (auto &[name, poolEntry] : threadPools_) {
    if (poolEntry.pool) {
      poolEntry.pool->wait();
      poolEntry.pool.reset();
    }
  }
  threadPools_.clear();
}

bool ThreadManager::createPool(const ThreadPoolConfig &config) {
  std::lock_guard<std::mutex> lock(threadMutex_);

  if (threadPools_.find(config.name) != threadPools_.end()) {
    return false; // Pool already exists
  }

  // Validate thread count
  uint32_t threadCount = config.threadCount > 0 ? config.threadCount : 1;

  PoolEntry entry;
  entry.config = config;
  entry.config.threadCount = threadCount;
  entry.pool = std::make_unique<BS::thread_pool<features>>(threadCount);
  threadPools_[config.name] = std::move(entry);

  return true;
}

BS::thread_pool<ThreadManager::features> *
ThreadManager::getPool(const std::string &name) {
  std::lock_guard<std::mutex> lock(threadMutex_);

  auto it = threadPools_.find(name);
  if (it == threadPools_.end()) {
    return nullptr;
  }
  return it->second.pool.get();
}

bool ThreadManager::hasPool(const std::string &name) const {
  std::lock_guard<std::mutex> lock(threadMutex_);
  return threadPools_.find(name) != threadPools_.end();
}

bool ThreadManager::destroyPool(const std::string &name) {
  std::lock_guard<std::mutex> lock(threadMutex_);

  auto it = threadPools_.find(name);
  if (it == threadPools_.end()) {
    return false;
  }

  // Wait for all tasks and destroy
  if (it->second.pool) {
    it->second.pool->wait();
    it->second.pool.reset();
  }

  threadPools_.erase(it);
  return true;
}

bool ThreadManager::resizePool(const std::string &name,
                               uint32_t newThreadCount) {
  // Validate thread count
  uint32_t actualThreadCount = newThreadCount > 0 ? newThreadCount : 1;

  std::function<void(uint32_t)> callback;
  uint32_t oldThreadCount = 0;

  {
    std::lock_guard<std::mutex> lock(threadMutex_);

    auto it = threadPools_.find(name);
    if (it == threadPools_.end()) {
      return false;
    }

    if (it->second.pool) {
      it->second.pool->pause();
      it->second.pool->reset(actualThreadCount);
      it->second.pool->unpause();
    }

    // Update config
    oldThreadCount = it->second.config.threadCount;
    it->second.config.threadCount = actualThreadCount;

    // Copy callback for invocation outside lock
    if (it->second.config.onReconfigure &&
        oldThreadCount != actualThreadCount) {
      callback = it->second.config.onReconfigure;
    }
  }

  // Notify callback outside lock to avoid deadlock
  if (callback) {
    callback(actualThreadCount);
  }

  return true;
}

void ThreadManager::waitAll(const std::string &name) {
  std::lock_guard<std::mutex> lock(threadMutex_);

  auto it = threadPools_.find(name);
  if (it != threadPools_.end() && it->second.pool) {
    it->second.pool->wait();
  }
}

void ThreadManager::waitAllPools() {
  std::lock_guard<std::mutex> lock(threadMutex_);

  for (auto &pair : threadPools_) {
    if (pair.second.pool) {
      pair.second.pool->wait();
    }
  }
}

std::vector<std::string> ThreadManager::getPoolNames() const {
  std::lock_guard<std::mutex> lock(threadMutex_);

  std::vector<std::string> names;
  names.reserve(threadPools_.size());
  for (const auto &pair : threadPools_) {
    names.push_back(pair.first);
  }
  return names;
}

ThreadPoolConfig ThreadManager::getPoolConfig(const std::string &name) const {
  std::lock_guard<std::mutex> lock(threadMutex_);

  auto it = threadPools_.find(name);
  if (it != threadPools_.end()) {
    return it->second.config;
  }
  return ThreadPoolConfig{};
}

// ========== Configuration Management ==========

void ThreadManager::applyConfig(const ThreadManagerConfig &config) {
  // Collect callbacks to invoke outside the lock
  std::vector<std::pair<std::function<void(uint32_t)>, uint32_t>> callbacks;

  {
    std::lock_guard<std::mutex> lock(threadMutex_);

    uint32_t totalThreads = config.totalThreads;
    if (totalThreads == 0)
      totalThreads = std::max(1u, std::thread::hardware_concurrency());

    totalWorkerCount_ = static_cast<uint32_t>(totalThreads);

    // Collect GPU pool callbacks for invocation outside lock
    for (auto &pair : threadPools_) {
      if (pair.second.config.type == PoolType::GPU &&
          pair.second.config.onReconfigure) {
        callbacks.emplace_back(pair.second.config.onReconfigure,
                               pair.second.config.threadCount);
      }
    }

    currentConfig_ = config;
    currentConfig_.totalThreads = totalThreads;
  }

  // Notify GPU pools outside lock to avoid deadlock
  for (auto &[callback, threadCount] : callbacks) {
    callback(threadCount);
  }
}

ThreadManagerConfig ThreadManager::getConfig() const {
  std::lock_guard<std::mutex> lock(threadMutex_);
  return currentConfig_;
}

// ========== Statistics ==========

uint32_t ThreadManager::getPoolThreadCount(const std::string &name) const {
  std::lock_guard<std::mutex> lock(threadMutex_);

  auto it = threadPools_.find(name);
  if (it != threadPools_.end() && it->second.pool) {
    return static_cast<uint32_t>(it->second.pool->get_thread_count());
  }
  return 0;
}

uint32_t ThreadManager::totalThreadCount() const {
  std::lock_guard<std::mutex> lock(threadMutex_);

  uint32_t total = 0;

  for (const auto &pair : threadPools_) {
    if (pair.second.pool) {
      total += static_cast<uint32_t>(pair.second.pool->get_thread_count());
    }
  }

  return total;
}

} // namespace core
