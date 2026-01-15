#include "thread_manager.h"
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <stdexcept>

namespace core {

ThreadManager &ThreadManager::instance() {
  static ThreadManager instance;
  return instance;
}

ThreadManager::ThreadManager() = default;

ThreadManager::~ThreadManager() { shutdown(); }

void ThreadManager::initialize(uint32_t workerCount) {
  if (up_pool)
    return;

  if (workerCount == 0)
    workerCount = std::max(1u, std::thread::hardware_concurrency());
  up_pool = std::make_unique<BS::thread_pool<features>>(workerCount);
  _totalWorkerCount = workerCount;
}

void ThreadManager::shutdown() {
  if (up_pool) {
    up_pool->wait();
    up_pool.reset();
  }
}

void ThreadManager::waitAll() { up_pool->wait(); }

void ThreadManager::reserveDeviceThread(int reserve) {
  std::lock_guard<std::mutex> lock(_poolMutex);
  up_pool->pause();
  _reservedWorkerCount += reserve;
  if (_reservedWorkerCount >= _totalWorkerCount)
    throw std::runtime_error(
        "reserved worker count is to big - it can't be bigger "
        "than total worker count");
  up_pool->reset(_totalWorkerCount - _reservedWorkerCount);
  up_pool->unpause();
}

void ThreadManager::releaseDeviceThread(int release) {
  std::lock_guard<std::mutex> lock(_poolMutex);
  up_pool->pause();
  _reservedWorkerCount -= release;
  if (_reservedWorkerCount <= 0)
    throw std::runtime_error(
        "reserved worker count is to low - it can't be 0 or lower");
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

} // namespace core
