#include "thread_manager.h"
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <stdexcept>

namespace core {

#ifdef ENGINE_DEBUG
static ThreadManager *g_threadManagerInstance = nullptr;
#endif

ThreadManager &ThreadManager::instance() {
#ifdef ENGINE_DEBUG
  if (g_threadManagerInstance) {
    return *g_threadManagerInstance;
  }
#endif
  static ThreadManager instance;
#ifdef ENGINE_DEBUG
  g_threadManagerInstance = &instance;
#endif
  return instance;
}

#ifdef ENGINE_DEBUG
void ThreadManager::setInstance(ThreadManager *inst) {
  g_threadManagerInstance = inst;
}

ThreadManager *ThreadManager::getInstance() { return g_threadManagerInstance; }
#endif

ThreadManager::ThreadManager() = default;

ThreadManager::~ThreadManager() { shutdown(); }

void ThreadManager::initialize(uint32_t workerCount) {
  ThreadManagerConfig config;
  config.totalThreads = workerCount;
  initialize(config);
}

void ThreadManager::initialize(const ThreadManagerConfig &config) {
  if (up_pool)
    return;

  currentConfig = config;

  uint32_t totalThreads = config.totalThreads;
  if (totalThreads == 0)
    totalThreads = std::max(1u, std::thread::hardware_concurrency());

  // Reserve threads for specialized pools, ensuring at least 1 worker thread
  uint32_t reservedThreads = config.loopThreads + config.gpuThreads;
  uint32_t workerThreads =
      (totalThreads > reservedThreads) ? (totalThreads - reservedThreads) : 1;

  up_pool = std::make_unique<BS::thread_pool<features>>(workerThreads);
  _totalWorkerCount = static_cast<uint32_t>(totalThreads);

  currentConfig.totalThreads = totalThreads;
}

void ThreadManager::shutdown() {
  std::lock_guard<std::mutex> lock(_poolMutex);

  // Shutdown all named pools
  for (auto &pair : namedPools) {
    if (pair.second.pool) {
      pair.second.pool->wait();
      pair.second.pool.reset();
    }
  }
  namedPools.clear();

  // Shutdown default pool
  if (up_pool) {
    up_pool->wait();
    up_pool.reset();
  }
}

void ThreadManager::waitAll() {
  if (up_pool) {
    up_pool->wait();
  }
}

void ThreadManager::reserveDeviceThread(int reserve) {
  std::lock_guard<std::mutex> lock(_poolMutex);
  up_pool->pause();
  _reservedWorkerCount += reserve;
  if (_reservedWorkerCount >= _totalWorkerCount)
    throw std::runtime_error(
        "reserved worker count is too big - it can't be bigger "
        "than total worker count");
  up_pool->reset(_totalWorkerCount - _reservedWorkerCount);
  up_pool->unpause();
}

void ThreadManager::releaseDeviceThread(int release) {
  std::lock_guard<std::mutex> lock(_poolMutex);
  up_pool->pause();
  // Check for underflow before subtraction
  if (static_cast<uint32_t>(release) > _reservedWorkerCount) {
    up_pool->unpause();
    throw std::runtime_error(
        "cannot release more threads than are currently reserved");
  }
  _reservedWorkerCount -= release;
  up_pool->reset(_totalWorkerCount - _reservedWorkerCount);
  up_pool->unpause();
}

void ThreadManager::resetPoolSize(uint32_t threads) {
  std::lock_guard<std::mutex> lock(_poolMutex);
  up_pool->pause();
  if (threads < _reservedWorkerCount)
    throw std::runtime_error(
        "new amount of workers is lower than the reserved amount");
  _totalWorkerCount = threads;
  up_pool->reset(threads - _reservedWorkerCount);
  up_pool->unpause();
}

// ========== Named Pool Management ==========

bool ThreadManager::createPool(const ThreadPoolConfig &config) {
  std::lock_guard<std::mutex> lock(_poolMutex);

  if (namedPools.find(config.name) != namedPools.end()) {
    return false; // Pool already exists
  }

  // Validate thread count
  uint32_t threadCount = config.threadCount > 0 ? config.threadCount : 1;

  PoolEntry entry;
  entry.config = config;
  entry.config.threadCount = threadCount;
  entry.pool = std::make_unique<BS::thread_pool<features>>(threadCount);
  namedPools[config.name] = std::move(entry);

  return true;
}

BS::thread_pool<ThreadManager::features> *
ThreadManager::getPool(const std::string &name) {
  std::lock_guard<std::mutex> lock(_poolMutex);

  auto it = namedPools.find(name);
  if (it == namedPools.end()) {
    return nullptr;
  }
  return it->second.pool.get();
}

bool ThreadManager::hasPool(const std::string &name) const {
  std::lock_guard<std::mutex> lock(_poolMutex);
  return namedPools.find(name) != namedPools.end();
}

bool ThreadManager::destroyPool(const std::string &name) {
  std::lock_guard<std::mutex> lock(_poolMutex);

  auto it = namedPools.find(name);
  if (it == namedPools.end()) {
    return false;
  }

  // Wait for all tasks and destroy
  if (it->second.pool) {
    it->second.pool->wait();
    it->second.pool.reset();
  }

  namedPools.erase(it);
  return true;
}

bool ThreadManager::resizePool(const std::string &name,
                               uint32_t newThreadCount) {
  // Validate thread count
  uint32_t actualThreadCount = newThreadCount > 0 ? newThreadCount : 1;

  std::function<void(uint32_t)> callback;
  uint32_t oldThreadCount = 0;

  {
    std::lock_guard<std::mutex> lock(_poolMutex);

    auto it = namedPools.find(name);
    if (it == namedPools.end()) {
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
    if (it->second.config.onReconfigure && oldThreadCount != actualThreadCount) {
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
  std::lock_guard<std::mutex> lock(_poolMutex);

  auto it = namedPools.find(name);
  if (it != namedPools.end() && it->second.pool) {
    it->second.pool->wait();
  }
}

void ThreadManager::waitAllPools() {
  std::lock_guard<std::mutex> lock(_poolMutex);

  // Wait for default pool
  if (up_pool) {
    up_pool->wait();
  }

  // Wait for all named pools
  for (auto &pair : namedPools) {
    if (pair.second.pool) {
      pair.second.pool->wait();
    }
  }
}

std::vector<std::string> ThreadManager::getPoolNames() const {
  std::lock_guard<std::mutex> lock(_poolMutex);

  std::vector<std::string> names;
  names.reserve(namedPools.size());
  for (const auto &pair : namedPools) {
    names.push_back(pair.first);
  }
  return names;
}

ThreadPoolConfig ThreadManager::getPoolConfig(const std::string &name) const {
  std::lock_guard<std::mutex> lock(_poolMutex);

  auto it = namedPools.find(name);
  if (it != namedPools.end()) {
    return it->second.config;
  }
  return ThreadPoolConfig{};
}

// ========== Configuration Management ==========

void ThreadManager::applyConfig(const ThreadManagerConfig &config) {
  // Collect callbacks to invoke outside the lock
  std::vector<std::pair<std::function<void(uint32_t)>, uint32_t>> callbacks;

  {
    std::lock_guard<std::mutex> lock(_poolMutex);

    uint32_t totalThreads = config.totalThreads;
    if (totalThreads == 0)
      totalThreads = std::max(1u, std::thread::hardware_concurrency());

    // Calculate new worker thread count, ensuring at least 1 worker thread
    uint32_t reservedThreads = config.loopThreads + config.gpuThreads;
    uint32_t workerThreads =
        (totalThreads > reservedThreads) ? (totalThreads - reservedThreads) : 1;

    // Ensure we have enough threads for reserved workers
    uint32_t actualWorkerThreads =
        (workerThreads > _reservedWorkerCount)
            ? (workerThreads - _reservedWorkerCount)
            : 1;

    // Resize default pool
    if (up_pool) {
      up_pool->pause();
      up_pool->reset(actualWorkerThreads);
      up_pool->unpause();
    }

    _totalWorkerCount = static_cast<uint32_t>(totalThreads);

    // Collect GPU pool callbacks for invocation outside lock
    for (auto &pair : namedPools) {
      if (pair.second.config.type == PoolType::GPU &&
          pair.second.config.onReconfigure) {
        callbacks.emplace_back(pair.second.config.onReconfigure,
                               pair.second.config.threadCount);
      }
    }

    currentConfig = config;
    currentConfig.totalThreads = totalThreads;
  }

  // Notify GPU pools outside lock to avoid deadlock
  for (auto &[callback, threadCount] : callbacks) {
    callback(threadCount);
  }
}

ThreadManagerConfig ThreadManager::getConfig() const {
  std::lock_guard<std::mutex> lock(_poolMutex);
  return currentConfig;
}

// ========== Statistics ==========

uint32_t ThreadManager::getPoolThreadCount(const std::string &name) const {
  std::lock_guard<std::mutex> lock(_poolMutex);

  auto it = namedPools.find(name);
  if (it != namedPools.end() && it->second.pool) {
    return static_cast<uint32_t>(it->second.pool->get_thread_count());
  }
  return 0;
}

uint32_t ThreadManager::totalThreadCount() const {
  std::lock_guard<std::mutex> lock(_poolMutex);

  uint32_t total = 0;

  // Default pool
  if (up_pool) {
    total += static_cast<uint32_t>(up_pool->get_thread_count());
  }

  // Named pools
  for (const auto &pair : namedPools) {
    if (pair.second.pool) {
      total += static_cast<uint32_t>(pair.second.pool->get_thread_count());
    }
  }

  return total;
}

} // namespace core
