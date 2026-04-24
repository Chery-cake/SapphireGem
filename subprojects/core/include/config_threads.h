#ifndef CONFIG_THREADS_H_
#define CONFIG_THREADS_H_

#include "config.h"
#include "core_export.h"
#include <cstdint>
#include <mutex>

namespace core {

/**
 * @brief Thread pool allocation configuration
 *
 * Specifies how many threads should be dedicated to each type of pool,
 * adjusting for the number of loops and GPUs.
 *
 * @note loopThreads and gpuThreads are "per loop" and "per GPU" values.
 *       When calling getEffectiveThreadAllocation(), these values are
 *       multiplied by mainLoopCount and gpuCount respectively to get
 *       the total thread counts.
 */
struct CORE_API ThreadPoolAllocation {
  uint32_t workerThreads = 0; // 0 = auto-detect based on hardware
  uint32_t loopThreads =
      1; // Threads per main loop (multiplied by mainLoopCount)
  uint32_t gpuThreads =
      1; // Threads per GPU (multiplied by gpuCount if multi-GPU enabled)

  bool operator==(const ThreadPoolAllocation &other) const {
    return workerThreads == other.workerThreads &&
           loopThreads == other.loopThreads && gpuThreads == other.gpuThreads;
  }

  bool operator!=(const ThreadPoolAllocation &other) const {
    return !(*this == other);
  }
};

/**
 * @brief GPU configuration settings
 */
struct CORE_API GPUConfig {
  uint32_t gpuCount = 1;          // Number of GPUs to use
  uint32_t preferredGPUIndex = 0; // Preferred GPU index for primary rendering
  bool enableMultiGPU = false;    // Enable multi-GPU rendering

  bool operator==(const GPUConfig &other) const {
    return gpuCount == other.gpuCount &&
           preferredGPUIndex == other.preferredGPUIndex &&
           enableMultiGPU == other.enableMultiGPU;
  }

  bool operator!=(const GPUConfig &other) const { return !(*this == other); }
};

/**
 * @brief Loop configuration settings
 */
struct CORE_API LoopConfig {
  uint32_t mainLoopCount = 1; // Number of main loops (e.g., for multi-window)
  uint32_t targetFrameRate = 60;  // Target frame rate (0 = unlimited)
  bool enableVSync = true;        // Enable vertical sync
  uint32_t maxFramesInFlight = 2; // Maximum frames that can be in flight

  bool operator==(const LoopConfig &other) const {
    return mainLoopCount == other.mainLoopCount &&
           targetFrameRate == other.targetFrameRate &&
           enableVSync == other.enableVSync &&
           maxFramesInFlight == other.maxFramesInFlight;
  }

  bool operator!=(const LoopConfig &other) const { return !(*this == other); }
};

class CORE_API ThreadsConfig {
public:
  // ========== Thread Pool Configuration ==========

  /**
   * @brief Set thread pool allocation
   * @param allocation New thread pool allocation
   *
   * Triggers thread pool configuration change callbacks if values differ.
   */
  void setThreadPoolAllocation(const ThreadPoolAllocation &allocation);

  /**
   * @brief Get current thread pool allocation
   * @return Current thread pool allocation
   */
  ThreadPoolAllocation getThreadPoolAllocation() const;

  /**
   * @brief Calculate effective thread counts based on GPUs and loops
   * @return Calculated thread counts considering hardware
   */
  ThreadPoolAllocation getEffectiveThreadAllocation() const;

  // ========== GPU Configuration ==========

  /**
   * @brief Set GPU configuration
   * @param config New GPU configuration
   *
   * Triggers GPU configuration change callbacks if values differ.
   */
  void setGPUConfig(const GPUConfig &config);

  /**
   * @brief Get current GPU configuration
   * @return Current GPU configuration
   */
  GPUConfig getGPUConfig() const;

  // ========== Loop Configuration ==========

  /**
   * @brief Set loop configuration
   * @param config New loop configuration
   *
   * Triggers loop configuration change callbacks if values differ.
   */
  void setLoopConfig(const LoopConfig &config);

  /**
   * @brief Get current loop configuration
   * @return Current loop configuration
   */
  LoopConfig getLoopConfig() const;

  const ThreadPoolAllocation &getThreadPoolAllocation() {
    return threadPoolAllocation_;
  }

  const GPUConfig &getGpuConfig() { return gpuConfig_; }

  const LoopConfig &getLoopConfig() { return loopConfig_; }

  bool operator==(const ThreadsConfig &other) const {
    return threadPoolAllocation_ == other.threadPoolAllocation_ &&
           gpuConfig_ == other.gpuConfig_ && loopConfig_ == other.loopConfig_;
  }

  bool operator!=(const ThreadsConfig &other) const {
    return !(*this == other);
  }

  ThreadsConfig &operator=(const ThreadsConfig &other) {
    if (this == &other)
      return *this; // Handle self-assignment

    threadPoolAllocation_ = other.threadPoolAllocation_;
    gpuConfig_ = other.gpuConfig_;
    loopConfig_ = other.loopConfig_;

    return *this;
  }

  ThreadsConfig &operator=(ThreadsConfig &&other) noexcept {
    if (this == &other)
      return *this; // Handle self-assignment

    threadPoolAllocation_ = std::move(other.threadPoolAllocation_);
    gpuConfig_ = std::move(other.gpuConfig_);
    loopConfig_ = std::move(other.loopConfig_);

    return *this;
  }

  explicit ThreadsConfig(ConfigSection &pendingChanges, bool &immediateMode,
                         signal::Signal<void()> &threadPoolChanged,
                         signal::Signal<void()> &gpuChanged,
                         signal::Signal<void()> &loopChanged,
                         std::mutex &configMutex);
  ~ThreadsConfig();

  void resetToDefaults();

private:
  ConfigSection &pendingChanges;
  bool &immediateMode;
  signal::Signal<void()> &threadPoolChanged;
  signal::Signal<void()> &gpuChanged;
  signal::Signal<void()> &loopChanged;
  std::mutex &configMutex;

  ThreadPoolAllocation threadPoolAllocation_;
  GPUConfig gpuConfig_;
  LoopConfig loopConfig_;

  mutable std::mutex conigThreadsMutex_;
};

} // namespace core

#endif // CONFIG_THREADS_H_
