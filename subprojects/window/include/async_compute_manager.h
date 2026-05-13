#ifndef ASYNC_COMPUTE_MANAGER_H_
#define ASYNC_COMPUTE_MANAGER_H_

#include "object.h"
#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"
#include "window_export.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

// Forward declarations
namespace device {
class GPUDevice;
} // namespace device

namespace window {

/**
 * @brief Priority level for async compute effects.
 *
 * Higher-priority effects are recorded first in the compute command buffer so
 * they complete earliest; downstream effects that depend on their output can
 * overlap during the same submission.
 */
enum class ComputePriority : uint8_t {
  Low    = 0, ///< Background work (e.g. precomputed caches).
  Normal = 1, ///< Default priority for displacement/wave effects.
  High   = 2, ///< Latency-sensitive effects (e.g. physics-driven deform).
};

/**
 * @brief Per-device async compute manager.
 *
 * Accepts compute work registrations from @ref RenderComponent objects (or
 * any other producer), records a single compute command buffer per frame, and
 * submits it to the dedicated compute queue using a timeline semaphore to
 * signal buffer readiness.
 *
 * Usage:
 * @code
 *   // Scene load:
 *   manager.registerEffect(&rc, ComputePriority::Normal,
 *       [&rc](vk::CommandBuffer cmd, uint32_t fi) {
 *           rc.recordComputeCommands(cmd, fi);
 *       });
 *
 *   // Per-window renderFrame (before graphics queue submission):
 *   manager.executeFrame(frameIndex, device.getComputeQueue());
 *   // Then add manager.getTimelineSemaphore() / getLastSubmittedValue()
 *   // to the graphics queue submission wait list.
 * @endcode
 *
 * Thread-safety: @ref registerEffect and @ref unregisterEffect are
 * mutex-protected.  @ref executeFrame must be called from a single thread
 * (the render thread).
 */
class WINDOW_API AsyncComputeManager {
public:
  AsyncComputeManager() = default;
  ~AsyncComputeManager();

  // Non-copyable, non-moveable
  AsyncComputeManager(const AsyncComputeManager &) = delete;
  AsyncComputeManager &operator=(const AsyncComputeManager &) = delete;
  AsyncComputeManager(AsyncComputeManager &&) = delete;
  AsyncComputeManager &operator=(AsyncComputeManager &&) = delete;

  // ── Lifecycle ──────────────────────────────────────────────────────────

  /**
   * @brief Initialise the manager for a given device.
   *
   * Creates one compute command buffer per frame-in-flight and a timeline
   * semaphore (initial value 0).  Must be called before any other method.
   *
   * @param device        The GPU device whose compute queue will be used.
   * @param framesInFlight Number of frames that can be in-flight concurrently.
   * @return true on success.
   */
  bool initialize(device::GPUDevice &device, uint32_t framesInFlight);

  /**
   * @brief Release all Vulkan resources.  Implicitly called by destructor.
   */
  void shutdown();

  // ── Effect registration ─────────────────────────────────────────────────

  /**
   * @brief Register a compute effect.
   *
   * @param renderable  Non-owning pointer used as a unique identifier.
   * @param prio        Execution priority (higher = recorded first).
   * @param recordFn    Callback invoked by @ref executeFrame to record the
   *                    compute dispatch commands into the command buffer.
   *
   * If @p renderable is already registered it is silently updated.
   */
  void registerEffect(
      ecs::component::object::RenderComponentBase *renderable,
      ComputePriority prio,
      std::function<void(vk::CommandBuffer, uint32_t)> recordFn);

  /**
   * @brief Unregister a compute effect.  No-op if not found.
   */
  void unregisterEffect(
      const ecs::component::object::RenderComponentBase *renderable);

  // ── Per-frame execution ─────────────────────────────────────────────────

  /**
   * @brief Record and submit compute work for the current frame.
   *
   * Steps performed:
   * 1. Sort the registered entries by priority (descending).
   * 2. Reset and begin the command buffer for @p frameIdx.
   * 3. Call each entry's record callback, passing the command buffer and
   *    @p frameIdx.
   * 4. End the command buffer.
   * 5. Submit to @p computeQueue, signalling the timeline semaphore with the
   *    next monotonically-increasing value.
   *
   * The graphics queue should wait on @ref getTimelineSemaphore() /
   * @ref getLastSubmittedValue() at the vertex-input stage before reading
   * displaced-position buffers.
   *
   * No-op if there are no registered effects.
   *
   * @param frameIdx      Frame-in-flight index (0 .. framesInFlight-1).
   * @param computeQueue  The Vulkan compute queue to submit to.
   */
  void executeFrame(uint32_t frameIdx, vk::Queue computeQueue);

  // ── Synchronisation accessors ───────────────────────────────────────────

  /**
   * @brief The timeline semaphore signalled by @ref executeFrame.
   *
   * Returns a null handle if not yet initialised.
   */
  [[nodiscard]] vk::Semaphore getTimelineSemaphore() const;

  /**
   * @brief The semaphore value most recently submitted (or 0 if none yet).
   */
  [[nodiscard]] uint64_t getLastSubmittedValue() const {
    return semaphoreValue_.load(std::memory_order_acquire);
  }

  /**
   * @brief Returns true if any effects are registered.
   */
  [[nodiscard]] bool hasEffects() const;

  /**
   * @brief CPU-side wait until all submitted compute work has completed.
   *
   * Waits on the timeline semaphore for @ref getLastSubmittedValue so the
   * caller knows that any compute output consumed by pending graphics commands
   * has been written.
   *
   * Typical usage: call this when an entity is about to be destroyed so its
   * SSBO / staging buffers are safe to free.
   *
   * No-op if no work has been submitted (@c getLastSubmittedValue == 0) or if
   * the manager is not initialized.
   *
   * @param timeoutNs  Wait timeout in nanoseconds.  Default: 5 seconds.
   * @return @c true if the semaphore reached the expected value within the
   *         timeout; @c false on timeout or if not initialized.
   */
  bool drainPending(uint64_t timeoutNs = 5'000'000'000ULL);

  [[nodiscard]] bool isInitialized() const { return initialized_; }

private:
  struct EffectEntry {
    ecs::component::object::RenderComponentBase *renderable = nullptr;
    ComputePriority prio = ComputePriority::Normal;
    std::function<void(vk::CommandBuffer, uint32_t)> recordFn;
  };

  std::vector<EffectEntry> entries_;
  mutable std::mutex mutex_;

  // Command recording resources (one cmd buf per frame-in-flight)
  std::unique_ptr<vk::raii::CommandPool> commandPool_;
  // Raw handles – lifetime tied to commandPool_
  std::vector<vk::CommandBuffer> commandBuffers_;
  // RAII wrapper that owns the allocated buffers
  std::unique_ptr<vk::raii::CommandBuffers> commandBuffersRaii_;

  // Timeline semaphore for compute→graphics synchronisation
  std::unique_ptr<vk::raii::Semaphore> timelineSemaphore_;
  /// Monotonically-increasing counter of submitted semaphore signal values.
  /// Accessed from the render thread (executeFrame) and from any thread via
  /// drainPending / getLastSubmittedValue, so it must be atomic.
  std::atomic<uint64_t> semaphoreValue_{0};

  device::GPUDevice *device_  = nullptr;
  uint32_t framesInFlight_    = 0;
  bool initialized_           = false;
};

} // namespace window

#endif // ASYNC_COMPUTE_MANAGER_H_
