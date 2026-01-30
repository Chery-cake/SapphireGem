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

  // Reserve threads for specialized pools
  uint32_t workerThreads = totalThreads;
  if (workerThreads > config.loopThreads + config.gpuThreads) {
    workerThreads -= (config.loopThreads + config.gpuThreads);
  } else {
    workerThreads = std::max(1u, workerThreads);
  }

  up_pool = std::make_unique<BS::thread_pool<features>>(workerThreads);
  _totalWorkerCount = static_cast<uint8_t>(totalThreads);

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
  _reservedWorkerCount -= release;
  if (_reservedWorkerCount < 0)
    throw std::runtime_error(
        "reserved worker count is too low - it can't be negative");
  up_pool->reset(_totalWorkerCount - _reservedWorkerCount);
  up_pool->unpause();
}

void ThreadManager::resetPoolSize(uint8_t threads) {
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

  PoolEntry entry;
  entry.config = config;
  entry.pool = std::make_unique<BS::thread_pool<features>>(config.threadCount);
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
  std::lock_guard<std::mutex> lock(_poolMutex);

  auto it = namedPools.find(name);
  if (it == namedPools.end()) {
    return false;
  }

  if (it->second.pool) {
    it->second.pool->pause();
    it->second.pool->reset(newThreadCount);
    it->second.pool->unpause();
  }

  // Update config
  uint32_t oldThreadCount = it->second.config.threadCount;
  it->second.config.threadCount = newThreadCount;

  // Notify callback if set (for GPU implementations)
  if (it->second.config.onReconfigure && oldThreadCount != newThreadCount) {
    it->second.config.onReconfigure(newThreadCount);
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
  std::lock_guard<std::mutex> lock(_poolMutex);

  uint32_t totalThreads = config.totalThreads;
  if (totalThreads == 0)
    totalThreads = std::max(1u, std::thread::hardware_concurrency());

  // Calculate new worker thread count
  uint32_t workerThreads = totalThreads;
  if (workerThreads > config.loopThreads + config.gpuThreads) {
    workerThreads -= (config.loopThreads + config.gpuThreads);
  } else {
    workerThreads = std::max(1u, workerThreads);
  }

  // Resize default pool
  if (up_pool) {
    up_pool->pause();
    up_pool->reset(workerThreads - _reservedWorkerCount);
    up_pool->unpause();
  }

  _totalWorkerCount = static_cast<uint8_t>(totalThreads);

  // Notify GPU pools about configuration change
  for (auto &pair : namedPools) {
    if (pair.second.config.type == PoolType::GPU &&
        pair.second.config.onReconfigure) {
      pair.second.config.onReconfigure(pair.second.config.threadCount);
    }
  }

  currentConfig = config;
  currentConfig.totalThreads = totalThreads;
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
