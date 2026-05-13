#include "ssbo_manager.h"
#include <print>

namespace device {

void SSBOManager::initialize(VMAAllocator &allocator, GPUDevice &device) {
    allocator_ = &allocator;
    device_    = &device;
}

void SSBOManager::shutdown() {
    std::lock_guard lock(mutex_);
    // Invalidate all live handles so any held copies become stale.
    for (auto &[id, slot] : slots_) {
        slot.valid->store(false, std::memory_order_release);
    }
    slots_.clear();
    allocator_ = nullptr;
    device_    = nullptr;
}

SSBOHandle SSBOManager::allocate(vk::DeviceSize size,
                                  const std::string &debugName) {
    if (!allocator_ || size == 0) {
        return {};
    }

    AllocatedBuffer buf = allocator_->createStorageBuffer(size, debugName);
    if (!buf.isValid()) {
        std::println(stderr,
                     "[SSBOManager] Failed to create storage buffer '{}' ({}B)",
                     debugName, static_cast<uint64_t>(size));
        return {};
    }

    std::lock_guard lock(mutex_);
    const uint64_t id = nextId_++;
    auto &slot = slots_[id];
    slot.buffer = std::move(buf);
    slot.valid  = std::make_shared<std::atomic<bool>>(true);

    return SSBOHandle{id, slot.valid};
}

void SSBOManager::free(SSBOHandle &handle) {
    if (!handle.isValid()) {
        return;
    }

    std::lock_guard lock(mutex_);
    auto it = slots_.find(handle.id_);
    if (it == slots_.end()) {
        return;
    }

    it->second.valid->store(false, std::memory_order_release);
    slots_.erase(it);

    // Reset the caller's handle so future calls to isValid() return false
    // without having to query the shared flag (minor optimisation).
    handle = SSBOHandle{};
}

vk::Buffer SSBOManager::getBuffer(const SSBOHandle &handle) const {
    if (!handle.isValid()) {
        return {};
    }
    std::lock_guard lock(mutex_);
    const auto it = slots_.find(handle.id_);
    if (it == slots_.end()) {
        return {};
    }
    return it->second.buffer.getBuffer();
}

vk::DeviceSize SSBOManager::getSize(const SSBOHandle &handle) const {
    if (!handle.isValid()) {
        return 0;
    }
    std::lock_guard lock(mutex_);
    const auto it = slots_.find(handle.id_);
    if (it == slots_.end()) {
        return 0;
    }
    return it->second.buffer.size;
}

void *SSBOManager::map(const SSBOHandle &handle) const {
    if (!handle.isValid()) {
        return nullptr;
    }
    std::lock_guard lock(mutex_);
    const auto it = slots_.find(handle.id_);
    if (it == slots_.end()) {
        return nullptr;
    }
    return it->second.buffer.map();
}

void SSBOManager::unmap(const SSBOHandle &handle) const {
    if (!handle.isValid()) {
        return;
    }
    std::lock_guard lock(mutex_);
    const auto it = slots_.find(handle.id_);
    if (it == slots_.end()) {
        return;
    }
    it->second.buffer.unmap();
}

void SSBOManager::flush(const SSBOHandle &handle) const {
    if (!handle.isValid()) {
        return;
    }
    std::lock_guard lock(mutex_);
    const auto it = slots_.find(handle.id_);
    if (it == slots_.end()) {
        return;
    }
    it->second.buffer.flush();
}

std::size_t SSBOManager::allocationCount() const {
    std::lock_guard lock(mutex_);
    return slots_.size();
}

} // namespace device
