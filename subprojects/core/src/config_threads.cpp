#include "config_threads.h"
#include "config.h"
#include <algorithm>
#include <iterator>
#include <mutex>
#include <thread>

namespace core {

ThreadsConfig::ThreadsConfig(ConfigSection &pendingChanges, bool &immediateMode,
                             signal::Signal<void()> &threadPoolChanged,
                             signal::Signal<void()> &gpuChanged,
                             signal::Signal<void()> &loopChanged,
                             std::mutex &configMutex)
    : pendingChanges(pendingChanges), immediateMode(immediateMode),
      threadPoolChanged(threadPoolChanged), gpuChanged(gpuChanged),
      loopChanged(loopChanged), configMutex(configMutex) {
  // Auto-detect thread count
  threadPoolAllocation_.workerThreads =
      0; // Will be calculated based on hardware
  threadPoolAllocation_.loopThreads = 1;
  threadPoolAllocation_.gpuThreads = 1;

  // Default GPU config
  gpuConfig_.gpuCount = 1;
  gpuConfig_.preferredGPUIndex = 0;
  gpuConfig_.enableMultiGPU = false;

  // Default loop config
  loopConfig_.mainLoopCount = 1;
  loopConfig_.targetFrameRate = 60;
  loopConfig_.enableVSync = true;
}

ThreadsConfig::~ThreadsConfig() {}

void ThreadsConfig::resetToDefaults() {
  std::lock_guard<std::mutex> lock(conigThreadsMutex_);

  threadPoolAllocation_ = ThreadPoolAllocation{};
  gpuConfig_ = GPUConfig{};
  loopConfig_ = LoopConfig{};
}

// ========== Thread Pool Configuration ==========

void ThreadsConfig::setThreadPoolAllocation(
    const ThreadPoolAllocation &allocation) {
  bool changed = false;

  {
    std::lock_guard<std::mutex> lock(conigThreadsMutex_);
    if (threadPoolAllocation_ != allocation) {
      threadPoolAllocation_ = allocation;
      changed = true;

      if (!immediateMode) {
        std::lock_guard<std::mutex> lock(configMutex);
        pendingChanges = pendingChanges | ConfigSection::ThreadPool;
      }
    }
  }

  if (changed && immediateMode) {
    threadPoolChanged.emit();
  }
}

ThreadPoolAllocation ThreadsConfig::getThreadPoolAllocation() const {
  std::lock_guard<std::mutex> lock(conigThreadsMutex_);
  return threadPoolAllocation_;
}

ThreadPoolAllocation ThreadsConfig::getEffectiveThreadAllocation() const {
  std::lock_guard<std::mutex> lock(conigThreadsMutex_);

  ThreadPoolAllocation effective = threadPoolAllocation_;

  // Calculate total threads for loops and GPUs
  // Note: loopThreads and gpuThreads are "per loop" and "per GPU" values
  uint32_t totalLoopThreads = effective.loopThreads * loopConfig_.mainLoopCount;
  uint32_t totalGPUThreads = gpuConfig_.enableMultiGPU
                                 ? (effective.gpuThreads * gpuConfig_.gpuCount)
                                 : effective.gpuThreads;

  // Calculate effective worker threads if set to auto-detect
  if (effective.workerThreads == 0) {
    uint32_t hardwareThreads =
        std::max(1u, std::thread::hardware_concurrency());

    // Reserve threads for loops and GPUs
    uint32_t totalReserved = totalLoopThreads + totalGPUThreads;

    // Ensure we have at least 1 worker thread
    effective.workerThreads = (hardwareThreads > totalReserved)
                                  ? (hardwareThreads - totalReserved)
                                  : 1;
  }

  // Set the effective values (total threads, not per-loop/per-GPU)
  effective.loopThreads = totalLoopThreads;
  effective.gpuThreads = totalGPUThreads;

  return effective;
}

// ========== GPU Configuration ==========

void ThreadsConfig::setGPUConfig(const GPUConfig &config) {
  bool changed = false;

  {
    std::lock_guard<std::mutex> lock(conigThreadsMutex_);
    if (gpuConfig_ != config) {
      gpuConfig_ = config;
      changed = true;

      if (!immediateMode) {
        std::lock_guard<std::mutex> lock(configMutex);
        pendingChanges = pendingChanges | ConfigSection::GPU;
      }
    }
  }

  if (changed && immediateMode) {
    gpuChanged.emit();
  }
}

GPUConfig ThreadsConfig::getGPUConfig() const {
  std::lock_guard<std::mutex> lock(conigThreadsMutex_);
  return gpuConfig_;
}

// ========== Loop Configuration ==========

void ThreadsConfig::setLoopConfig(const LoopConfig &config) {
  bool changed = false;

  {
    std::lock_guard<std::mutex> lock(conigThreadsMutex_);
    if (loopConfig_ != config) {
      loopConfig_ = config;
      changed = true;

      if (!immediateMode) {
        std::lock_guard<std::mutex> lock(configMutex);
        pendingChanges = pendingChanges | ConfigSection::Loop;
      }
    }
  }

  if (changed && immediateMode) {
    loopChanged.emit();
  }
}

LoopConfig ThreadsConfig::getLoopConfig() const {
  std::lock_guard<std::mutex> lock(conigThreadsMutex_);
  return loopConfig_;
}

} // namespace core
