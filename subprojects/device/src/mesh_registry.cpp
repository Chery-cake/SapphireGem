#include "mesh_registry.h"
#include <filesystem>
#include <print>

namespace device {

void MeshRegistry::initialize(VMAAllocator &allocator, GPUDevice &device) {
    allocator_ = &allocator;
    device_    = &device;
}

void MeshRegistry::shutdown() {
    {
        std::unique_lock lock(slotsMutex_);
        for (auto &[id, slot] : slots_) {
            slot.valid->store(false, std::memory_order_release);
        }
        slots_.clear();
    }
    {
        std::lock_guard lock(watchMutex_);
        watches_.clear();
    }
    allocator_ = nullptr;
    device_    = nullptr;
}

MeshHandle MeshRegistry::registerMesh(MeshEntry entry,
                                        const std::string &sourcePath) {
    std::unique_lock lock(slotsMutex_);
    const uint64_t id = nextId_++;
    auto &slot = slots_[id];
    slot.entry      = std::move(entry);
    slot.sourcePath = sourcePath;
    slot.valid      = std::make_shared<std::atomic<bool>>(true);
    return MeshHandle{id, slot.valid};
}

void MeshRegistry::updateMesh(const MeshHandle &handle, MeshEntry entry) {
    if (!handle.isValid()) {
        return;
    }
    std::unique_lock lock(slotsMutex_);
    auto it = slots_.find(handle.id_);
    if (it != slots_.end()) {
        it->second.entry = std::move(entry);
    }
}

void MeshRegistry::unregisterMesh(MeshHandle &handle) {
    if (!handle.isValid()) {
        return;
    }
    {
        std::unique_lock lock(slotsMutex_);
        auto it = slots_.find(handle.id_);
        if (it == slots_.end()) {
            return;
        }
        it->second.valid->store(false, std::memory_order_release);
        slots_.erase(it);
    }
    handle = MeshHandle{};
}

bool MeshRegistry::lookup(const MeshHandle &handle, MeshEntry &outEntry) const {
    if (!handle.isValid()) {
        return false;
    }
    std::shared_lock lock(slotsMutex_);
    const auto it = slots_.find(handle.id_);
    if (it == slots_.end()) {
        return false;
    }
    outEntry = it->second.entry;
    return true;
}

void MeshRegistry::watchFile(const std::string &path,
                              std::function<void(const std::string &)> reloadFn) {
    std::lock_guard lock(watchMutex_);
    auto &entry = watches_[path];
    if (entry.callbacks.empty()) {
        // First registration: record current mtime as baseline
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(path, ec);
        if (!ec) {
            entry.baseline = t;
        } else {
            std::println("[MeshRegistry] Cannot stat '{}': {}", path, ec.message());
        }
    }
    entry.callbacks.push_back(std::move(reloadFn));
}

bool MeshRegistry::poll() {
    if (watches_.empty()) {
        return false;
    }

    std::lock_guard lock(watchMutex_);
    bool anyChanged = false;

    for (auto &[path, watchEntry] : watches_) {
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(path, ec);
        if (!ec && t != watchEntry.baseline) {
            anyChanged         = true;
            watchEntry.baseline = t;
            std::println("[MeshRegistry] '{}' changed — triggering reload.", path);
            for (auto &cb : watchEntry.callbacks) {
                if (cb) {
                    cb(path);
                }
            }
        }
    }

    return anyChanged;
}

std::size_t MeshRegistry::meshCount() const {
    std::shared_lock lock(slotsMutex_);
    return slots_.size();
}

std::size_t MeshRegistry::watchedFileCount() const {
    std::lock_guard lock(watchMutex_);
    return watches_.size();
}

} // namespace device
