#include "thread_manager.h"
#include <algorithm>
#include <cstdint>
#include <execution>
#include <mutex>
#include <numeric>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace core {

// Definition of the static signal member
signal::Signal<void(const std::string &, uint32_t)>
    ThreadPoolConfig::onReconfigure;

#ifdef ENGINE_DEBUG
static ThreadManager *g_threadManagerInstance = nullptr;
static std::mutex g_threadManagerMutex;

ThreadManager &ThreadManager::instance() {
  std::lock_guard<std::mutex> lock(g_threadManagerMutex);
  if (g_threadManagerInstance == nullptr) {
    g_threadManagerInstance = new ThreadManager();
  }
  return *g_threadManagerInstance;
}

void ThreadManager::setInstance(ThreadManager *inst) {
  // Called by hot reload system when coordinating instance swap.
  // Caller is responsible for managing the old instance's lifetime.
  std::lock_guard<std::mutex> lock(g_threadManagerMutex);
  g_threadManagerInstance = inst;
}

ThreadManager *ThreadManager::getInstance() {
  std::lock_guard<std::mutex> lock(g_threadManagerMutex);
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
  running_.store(false);

  auto waitQueue = threadPools_ | std::views::filter([](const auto &thread) {
                     return thread.second.pool != nullptr;
                   });
  std::for_each(std::execution::par_unseq, waitQueue.begin(), waitQueue.end(),
                [](auto &&thread) {
                  thread.second.pool->wait();
                  thread.second.pool.reset();
                });
  threadPools_.clear();
}

bool ThreadManager::createPool(const ThreadPoolConfig &config) {
  std::lock_guard<std::mutex> lock(threadMutex_);

  if (threadPools_.contains(config.name)) {
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
  return threadPools_.contains(name);
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
  bool resized = false;

  {
    std::lock_guard<std::mutex> lock(threadMutex_);

    auto it = threadPools_.find(name);
    if (it == threadPools_.end()) {
      return false;
    }

    if (it->second.config.threadCount != actualThreadCount) {
      if (it->second.pool) {
        it->second.pool->pause();
        it->second.pool->reset(actualThreadCount);
        it->second.pool->unpause();
      }

      // Update config
      it->second.config.threadCount = actualThreadCount;
      resized = true;
    }
  }

  // Notify callback outside lock to avoid deadlock
  if (resized) {
    ThreadPoolConfig::onReconfigure.emit(name, newThreadCount);
  }

  return true;
}

void ThreadManager::wait(const std::string &name) {
  std::lock_guard<std::mutex> lock(threadMutex_);

  std::ranges::find_if(threadPools_, [&name](const auto &pair) {
    if (pair.first == name && pair.second.pool) {
      pair.second.pool->wait();
      return true;
    }
    return false;
  });
}

void ThreadManager::waitAll() {
  std::lock_guard<std::mutex> lock(threadMutex_);

  std::for_each(std::execution::par_unseq, threadPools_.begin(),
                threadPools_.end(), [](auto &&pair) {
                  if (pair.second.pool) {
                    pair.second.pool->wait();
                  }
                });
}

std::vector<std::string> ThreadManager::getPoolNames() const {
  std::lock_guard<std::mutex> lock(threadMutex_);

  return threadPools_ |
         std::views::transform([](const auto &pair) { return pair.first; }) |
         std::ranges::to<std::vector>();
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
  std::vector<std::pair<std::string, uint32_t>> poolUpdates;

  {
    std::lock_guard<std::mutex> lock(threadMutex_);

    uint32_t totalThreads = config.totalThreads;
    if (totalThreads == 0) {
      totalThreads = std::max(1U, std::thread::hardware_concurrency());
    }

    totalWorkerCount_ = totalThreads;

    // Collect GPU pool callbacks for invocation outside lock

    poolUpdates = threadPools_ | std::views::transform([](const auto &pair) {
                    return std::pair<std::string, uint32_t>(
                        pair.first, pair.second.config.threadCount);
                  }) |
                  std::ranges::to<std::vector>();

    currentConfig_ = config;
    currentConfig_.totalThreads = totalThreads;
  }

  // Notify pools outside lock to avoid deadlock
  std::ranges::for_each(poolUpdates, [](auto &pair) {
    ThreadPoolConfig::onReconfigure.emit(pair.first, pair.second);
  });
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

  auto pools = threadPools_ | std::views::filter([](const auto &pair) {
                 return pair.second.pool != nullptr;
               }) |
               std::views::transform([](const auto &pair) {
                 return pair.second.pool->get_thread_count();
               });

  auto total = std::accumulate(pools.begin(), pools.end(), 0U);

  return total;
}

} // namespace core
