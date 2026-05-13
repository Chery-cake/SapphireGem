#include "async_compute_manager.h"
#include "vulkan_device.h"
#include <algorithm>
#include <mutex>
#include <print>
#include <ranges>

namespace window {

// ── Destructor ─────────────────────────────────────────────────────────────

AsyncComputeManager::~AsyncComputeManager() { shutdown(); }

// ── Lifecycle ──────────────────────────────────────────────────────────────

bool AsyncComputeManager::initialize(device::GPUDevice &device,
                                     uint32_t framesInFlight) {
  if (initialized_) {
    std::println(stderr, "[AsyncComputeManager] Already initialized");
    return false;
  }

  const auto &qf = device.getQueueFamilies();
  if (!qf.hasCompute()) {
    std::println(stderr,
                 "[AsyncComputeManager] Device has no compute queue family");
    return false;
  }

  device_          = &device;
  framesInFlight_  = framesInFlight;

  // ── Command pool ──────────────────────────────────────────────────────
  const uint32_t computeFamily = qf.computeFamily.value();
  try {
    commandPool_ = std::make_unique<vk::raii::CommandPool>(
        device.getRaiiDevice(),
        vk::CommandPoolCreateInfo{
            vk::CommandPoolCreateFlagBits::eResetCommandBuffer, computeFamily});
  } catch (const vk::SystemError &e) {
    std::println(stderr,
                 "[AsyncComputeManager] Failed to create command pool: {}",
                 e.what());
    return false;
  }

  // ── Command buffers (one per frame-in-flight) ──────────────────────────
  try {
    commandBuffersRaii_ = std::make_unique<vk::raii::CommandBuffers>(
        device.getRaiiDevice(),
        vk::CommandBufferAllocateInfo{**commandPool_,
                                      vk::CommandBufferLevel::ePrimary,
                                      framesInFlight});
  } catch (const vk::SystemError &e) {
    commandPool_.reset();
    std::println(stderr,
                 "[AsyncComputeManager] Failed to allocate command buffers: {}",
                 e.what());
    return false;
  }

  commandBuffers_.reserve(framesInFlight);
  for (auto &cb : *commandBuffersRaii_) {
    commandBuffers_.push_back(*cb);
  }

  // ── Timeline semaphore ─────────────────────────────────────────────────
  vk::SemaphoreTypeCreateInfo timelineTypeInfo{vk::SemaphoreType::eTimeline,
                                               0u};
  vk::SemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.pNext = &timelineTypeInfo;
  try {
    timelineSemaphore_ = std::make_unique<vk::raii::Semaphore>(
        device.getRaiiDevice(), semaphoreInfo);
  } catch (const vk::SystemError &e) {
    commandBuffersRaii_.reset();
    commandBuffers_.clear();
    commandPool_.reset();
    std::println(stderr,
                 "[AsyncComputeManager] Failed to create timeline semaphore: {}",
                 e.what());
    return false;
  }

  initialized_   = true;
  semaphoreValue_ = 0;
  std::println("[AsyncComputeManager] Initialised ({} frame slots)", framesInFlight);
  return true;
}

void AsyncComputeManager::shutdown() {
  if (!initialized_) {
    return;
  }

  if (device_) {
    device_->waitIdle();
  }

  {
    std::lock_guard lock(mutex_);
    entries_.clear();
  }

  timelineSemaphore_.reset();
  commandBuffersRaii_.reset();
  commandBuffers_.clear();
  commandPool_.reset();

  device_        = nullptr;
  initialized_   = false;
  semaphoreValue_ = 0;
  std::println("[AsyncComputeManager] Shutdown");
}

// ── Effect registration ────────────────────────────────────────────────────

void AsyncComputeManager::registerEffect(
    ecs::component::object::RenderComponentBase *renderable,
    ComputePriority prio,
    std::function<void(vk::CommandBuffer, uint32_t)> recordFn) {
  if (renderable == nullptr || !recordFn) {
    return;
  }

  std::lock_guard lock(mutex_);

  // Update existing entry if the same renderable is already registered
  for (auto &entry : entries_) {
    if (entry.renderable == renderable) {
      entry.prio     = prio;
      entry.recordFn = std::move(recordFn);
      return;
    }
  }

  entries_.push_back(EffectEntry{renderable, prio, std::move(recordFn)});
}

void AsyncComputeManager::unregisterEffect(
    const ecs::component::object::RenderComponentBase *renderable) {
  if (renderable == nullptr) {
    return;
  }

  std::lock_guard lock(mutex_);
  auto it = std::ranges::find_if(
      entries_,
      [renderable](const EffectEntry &e) { return e.renderable == renderable; });
  if (it != entries_.end()) {
    entries_.erase(it);
  }
}

// ── Per-frame execution ────────────────────────────────────────────────────

void AsyncComputeManager::executeFrame(uint32_t frameIdx,
                                       vk::Queue computeQueue) {
  if (!initialized_ || !computeQueue) {
    return;
  }

  // Snapshot + sort entries (lock only for the snapshot)
  std::vector<EffectEntry> snapshot;
  {
    std::lock_guard lock(mutex_);
    if (entries_.empty()) {
      return;
    }
    snapshot = entries_; // copy so we can release the lock before recording
  }

  // Sort by descending priority (highest executes first)
  std::ranges::sort(snapshot, [](const EffectEntry &a, const EffectEntry &b) {
    return static_cast<uint8_t>(a.prio) > static_cast<uint8_t>(b.prio);
  });

  // Select command buffer for this frame slot
  if (frameIdx >= commandBuffers_.size()) {
    std::println(stderr,
                 "[AsyncComputeManager] frameIdx {} out of range ({})",
                 frameIdx, commandBuffers_.size());
    return;
  }
  vk::CommandBuffer cmd = commandBuffers_[frameIdx];

  // Reset and record
  cmd.reset(vk::CommandBufferResetFlags{});
  cmd.begin(vk::CommandBufferBeginInfo{
      vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

  for (const auto &entry : snapshot) {
    if (entry.recordFn) {
      entry.recordFn(cmd, frameIdx);
    }
  }

  cmd.end();

  // Submit with timeline semaphore signal — increment counter only on success
  const uint64_t signalValue = semaphoreValue_.load(std::memory_order_relaxed) + 1;

  vk::CommandBufferSubmitInfo cmdInfo{cmd, 0};
  vk::SemaphoreSubmitInfo signalInfo{**timelineSemaphore_, signalValue,
                                     vk::PipelineStageFlagBits2::eComputeShader,
                                     0};

  vk::SubmitInfo2 submit2{{}, {}, cmdInfo, signalInfo};
  try {
    computeQueue.submit2(submit2);
    semaphoreValue_.store(signalValue, std::memory_order_release); // advance only on successful submit
  } catch (const vk::SystemError &e) {
    std::println(stderr,
                 "[AsyncComputeManager] Failed to submit compute work: {}",
                 e.what());
  }
}

// ── Synchronisation accessors ──────────────────────────────────────────────

vk::Semaphore AsyncComputeManager::getTimelineSemaphore() const {
  if (!timelineSemaphore_) {
    return {};
  }
  return **timelineSemaphore_;
}

bool AsyncComputeManager::hasEffects() const {
  std::lock_guard lock(mutex_);
  return !entries_.empty();
}

bool AsyncComputeManager::drainPending(uint64_t timeoutNs) {
  const uint64_t valueToWait = semaphoreValue_.load(std::memory_order_acquire);
  if (!initialized_ || valueToWait == 0) {
    return true; // Nothing submitted yet — trivially done.
  }
  if (!timelineSemaphore_) {
    return false;
  }

  const vk::Semaphore semHandle = **timelineSemaphore_;
  const vk::SemaphoreWaitInfo waitInfo{
      {},           // flags
      1,            // semaphore count
      &semHandle,   // pSemaphores
      &valueToWait  // pValues
  };

  try {
    const auto result = device_->getRaiiDevice().waitSemaphores(waitInfo, timeoutNs);
    if (result != vk::Result::eSuccess) {
      std::println(stderr,
                   "[AsyncComputeManager] drainPending: waitSemaphores returned {}",
                   vk::to_string(result));
      return false;
    }
  } catch (const vk::SystemError &e) {
    std::println(stderr,
                 "[AsyncComputeManager] drainPending: exception: {}", e.what());
    return false;
  }

  return true;
}

} // namespace window
