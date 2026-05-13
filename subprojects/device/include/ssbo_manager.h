#ifndef SSBO_MANAGER_H_
#define SSBO_MANAGER_H_

#include "device_export.h"
#include "vma_allocator.h"
#include "vulkan_device.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace device {

// ── SSBOHandle ────────────────────────────────────────────────────────────────

/**
 * @brief Lightweight copyable handle to a GPU storage buffer.
 *
 * An SSBOHandle refers to a slot in an @ref SSBOManager.  Handles are cheap
 * to copy — every copy refers to the *same* underlying allocation.
 *
 * When the owning entity (or the SSBOManager) invalidates the slot, the
 * underlying allocation is freed and @ref isValid returns @c false.  Any copy
 * of the handle automatically reflects the invalidation.
 *
 * ### Thread-safety
 * - @ref isValid and @ref getBuffer / @ref getSize are thread-safe (shared
 *   lock on the manager).
 * - @ref invalidate must be called through the owning @ref SSBOManager.
 */
class DEVICE_API SSBOHandle {
public:
    /// Sentinel: invalid / default-constructed handle.
    SSBOHandle() = default;

    /// Two handles are equal if and only if they share the same slot id
    /// in the same manager.
    bool operator==(const SSBOHandle &o) const noexcept {
        return id_ == o.id_;
    }
    bool operator!=(const SSBOHandle &o) const noexcept {
        return !(*this == o);
    }

    /// @return true if the backing buffer is still live.
    [[nodiscard]] bool isValid() const noexcept {
        return id_ != kInvalid && valid_ && valid_->load(std::memory_order_acquire);
    }

    /// The numeric slot id (opaque; for debug / logging only).
    [[nodiscard]] uint64_t id() const noexcept { return id_; }

private:
    friend class SSBOManager;

    /// Only the SSBOManager may construct a valid handle.
    SSBOHandle(uint64_t id, std::shared_ptr<std::atomic<bool>> valid)
        : id_(id), valid_(std::move(valid)) {}

    static constexpr uint64_t kInvalid = 0;
    uint64_t id_ = kInvalid;
    std::shared_ptr<std::atomic<bool>> valid_; ///< shared with the manager slot
};

// ── SSBOManager ───────────────────────────────────────────────────────────────

/**
 * @brief Per-device manager for Shader Storage Buffer Objects (SSBOs).
 *
 * ### Responsibilities
 * - Allocate and free GPU storage buffers backed by VMA.
 * - Issue @ref SSBOHandle objects that are safe to copy and to retain across
 *   multiple frames.
 * - Allow the owner to @ref invalidate a handle at any time; all existing
 *   copies of that handle will see @c isValid() == false without use-after-free.
 *
 * ### Per-device
 * Construct one @c SSBOManager per @ref GPUDevice.  When using multi-GPU,
 * each device needs its own manager (buffer pointers are not portable across
 * devices).
 *
 * ### Thread-safety
 * All public methods are mutex-protected and can be called concurrently.
 *
 * ### Usage
 * @code
 *   SSBOManager mgr;
 *   mgr.initialize(allocator, device);
 *
 *   // Allocate a 1 MiB storage buffer:
 *   auto handle = mgr.allocate(1024 * 1024, "MyData");
 *
 *   // On entity destruction:
 *   mgr.free(handle);   // buffer freed, all copies of handle become invalid
 * @endcode
 */
class DEVICE_API SSBOManager {
public:
    SSBOManager() = default;
    ~SSBOManager() = default;

    // Non-copyable (owns GPU resources)
    SSBOManager(const SSBOManager &) = delete;
    SSBOManager &operator=(const SSBOManager &) = delete;
    SSBOManager(SSBOManager &&) = delete;
    SSBOManager &operator=(SSBOManager &&) = delete;

    // ── Lifecycle ──────────────────────────────────────────────────────────

    /**
     * @brief Attach to a VMAAllocator and device.
     *
     * @param allocator  Allocator for this device (must outlive the manager).
     * @param device     GPU device (used for debug names; must outlive the mgr).
     */
    void initialize(VMAAllocator &allocator, GPUDevice &device);

    /**
     * @brief Free all managed buffers.
     */
    void shutdown();

    [[nodiscard]] bool isInitialized() const noexcept { return allocator_ != nullptr; }

    // ── Allocation ─────────────────────────────────────────────────────────

    /**
     * @brief Allocate a GPU storage buffer.
     *
     * @param size       Size in bytes.
     * @param debugName  Optional Vulkan debug name.
     * @return A new, valid @ref SSBOHandle.
     */
    [[nodiscard]] SSBOHandle allocate(vk::DeviceSize size,
                                       const std::string &debugName = "");

    /**
     * @brief Free a buffer and invalidate the handle.
     *
     * All copies of @p handle become invalid after this call.
     * No-op if @p handle is already invalid.
     */
    void free(SSBOHandle &handle);

    // ── Buffer access ──────────────────────────────────────────────────────

    /**
     * @brief Retrieve the raw Vulkan buffer for a handle.
     * @return The buffer, or a null handle if @p handle is invalid.
     */
    [[nodiscard]] vk::Buffer getBuffer(const SSBOHandle &handle) const;

    /**
     * @brief Retrieve the byte size for a handle.
     * @return Size in bytes, or 0 if @p handle is invalid.
     */
    [[nodiscard]] vk::DeviceSize getSize(const SSBOHandle &handle) const;

    /**
     * @brief Map the host-visible memory for writing.
     * @return Pointer to mapped memory, or nullptr.
     */
    [[nodiscard]] void *map(const SSBOHandle &handle) const;

    /**
     * @brief Unmap host-visible memory previously mapped with @ref map.
     */
    void unmap(const SSBOHandle &handle) const;

    /**
     * @brief Flush writes to a mapped buffer.
     */
    void flush(const SSBOHandle &handle) const;

    /**
     * @brief Number of live allocations.
     */
    [[nodiscard]] std::size_t allocationCount() const;

private:
    struct Slot {
        AllocatedBuffer              buffer;
        std::shared_ptr<std::atomic<bool>> valid =
            std::make_shared<std::atomic<bool>>(true);
    };

    VMAAllocator   *allocator_ = nullptr;
    GPUDevice      *device_    = nullptr;

    mutable std::mutex mutex_;
    uint64_t           nextId_ = 1; ///< Never wraps in practice
    std::unordered_map<uint64_t, Slot> slots_;
};

} // namespace device

#endif // SSBO_MANAGER_H_
