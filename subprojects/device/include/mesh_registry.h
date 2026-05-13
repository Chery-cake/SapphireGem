#ifndef MESH_REGISTRY_H_
#define MESH_REGISTRY_H_

#include "device_export.h"
#include "vma_allocator.h"
#include "vulkan_device.h"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace device {

// ── MeshHandle ────────────────────────────────────────────────────────────────

/**
 * @brief Copyable, invalidatable reference to a registered mesh.
 *
 * MeshHandle is the safe, copyable token returned by @ref MeshRegistry.
 * Like @ref SSBOHandle, all copies share an atomic validity flag.  When the
 * mesh is evicted from the registry (e.g. the source file is removed), every
 * copy of the handle becomes invalid without use-after-free.
 */
class DEVICE_API MeshHandle {
public:
    MeshHandle() = default;

    bool operator==(const MeshHandle &o) const noexcept { return id_ == o.id_; }
    bool operator!=(const MeshHandle &o) const noexcept { return !(*this == o); }

    [[nodiscard]] bool isValid() const noexcept {
        return id_ != kInvalid && valid_ && valid_->load(std::memory_order_acquire);
    }

    [[nodiscard]] uint64_t id() const noexcept { return id_; }

private:
    friend class MeshRegistry;

    MeshHandle(uint64_t id, std::shared_ptr<std::atomic<bool>> valid)
        : id_(id), valid_(std::move(valid)) {}

    static constexpr uint64_t kInvalid = 0;
    uint64_t id_ = kInvalid;
    std::shared_ptr<std::atomic<bool>> valid_;
};

// ── MeshEntry ─────────────────────────────────────────────────────────────────

/**
 * @brief Metadata and GPU buffers for a single registered mesh.
 *
 * The GPU buffers are allocated by the @ref MeshRegistry's @ref VMAAllocator.
 * Access is gated through @ref MeshHandle to prevent dangling-pointer bugs.
 */
struct DEVICE_API MeshEntry {
    std::string            sourcePath;     ///< Empty if not disk-backed.
    vk::Buffer             vertexBuffer{}; ///< Device-local vertex buffer.
    vk::DeviceSize         vertexSize  = 0;
    vk::Buffer             indexBuffer{}; ///< Device-local index buffer (may be null).
    vk::DeviceSize         indexSize   = 0;
    uint32_t               vertexCount = 0;
    uint32_t               indexCount  = 0;
    uint32_t               dimension   = 3; ///< Spatial dimension (2 or 3).

    /// Opaque user data pointer (e.g. a typed Mesh* for higher-level systems).
    void *userData = nullptr;
};

// ── MeshRegistry ─────────────────────────────────────────────────────────────

/**
 * @brief Per-device registry mapping mesh IDs to GPU vertex/index buffers.
 *
 * ### Per-device
 * Each @ref GPUDevice must have its own @c MeshRegistry because Vulkan buffer
 * handles are not portable across devices.
 *
 * ### Thread-safety
 * - @ref lookup (read path) uses a shared lock and is safe to call
 *   concurrently with other reads.
 * - @ref registerMesh / @ref unregisterMesh / @ref poll use an exclusive lock.
 *
 * ### File-change watching
 * For disk-backed meshes, call @ref watchFile with the source path and a
 * reload callback.  @ref poll (typically called once per frame) checks
 * modification times and fires the callback for any changed file.
 *
 * @code
 *   MeshRegistry registry;
 *   registry.initialize(allocator, device);
 *
 *   MeshHandle h = registry.registerMesh(entry, "assets/meshes/cube.bin");
 *   registry.watchFile("assets/meshes/cube.bin", [&](const std::string &path) {
 *       auto newEntry = loadMeshFromDisk(path, allocator, device);
 *       registry.updateMesh(h, newEntry);
 *   });
 *
 *   // Per-frame:
 *   registry.poll(); // fires callbacks for any modified files
 * @endcode
 */
class DEVICE_API MeshRegistry {
public:
    MeshRegistry() = default;
    ~MeshRegistry() = default;

    // Non-copyable, non-moveable
    MeshRegistry(const MeshRegistry &) = delete;
    MeshRegistry &operator=(const MeshRegistry &) = delete;
    MeshRegistry(MeshRegistry &&) = delete;
    MeshRegistry &operator=(MeshRegistry &&) = delete;

    // ── Lifecycle ──────────────────────────────────────────────────────────

    /**
     * @brief Attach the registry to a device's allocator.
     * @param allocator  Per-device VMA allocator (must outlive the registry).
     * @param device     The owning GPU device (for debug / logging).
     */
    void initialize(VMAAllocator &allocator, GPUDevice &device);

    /**
     * @brief Unregister all meshes and cancel all file watches.
     */
    void shutdown();

    [[nodiscard]] bool isInitialized() const noexcept { return allocator_ != nullptr; }

    // ── Registration ───────────────────────────────────────────────────────

    /**
     * @brief Register a mesh and obtain a handle.
     *
     * @param entry      Mesh metadata and pre-created GPU buffer handles.
     *                   The VkBuffer lifetime must be managed externally or by
     *                   the caller; the registry stores only handles and metadata.
     * @param sourcePath Optional path to the mesh source file (used for file
     *                   watching).  Empty = no watching.
     * @return A new, valid @ref MeshHandle.
     */
    [[nodiscard]] MeshHandle registerMesh(MeshEntry entry,
                                           const std::string &sourcePath = {});

    /**
     * @brief Replace the entry for an existing handle in-place.
     *
     * No-op if @p handle is invalid.
     */
    void updateMesh(const MeshHandle &handle, MeshEntry entry);

    /**
     * @brief Remove a mesh from the registry and invalidate the handle.
     *
     * All copies of @p handle become invalid after this call.
     * No-op if @p handle is already invalid.
     */
    void unregisterMesh(MeshHandle &handle);

    // ── Lookup ─────────────────────────────────────────────────────────────

    /**
     * @brief Look up mesh metadata by handle.
     *
     * @param handle   Handle to query.
     * @param outEntry Output; filled on success.
     * @return @c true if the handle is valid and the entry was found.
     */
    [[nodiscard]] bool lookup(const MeshHandle &handle,
                               MeshEntry &outEntry) const;

    // ── File watching ──────────────────────────────────────────────────────

    /**
     * @brief Register a callback to be fired when @p path changes on disk.
     *
     * Multiple callbacks can be registered for the same path.  The callback
     * receives the path that changed so one function can handle many files.
     *
     * @param path       Source file to watch.
     * @param reloadFn   Called on the @ref poll thread when the file changes.
     */
    void watchFile(const std::string &path,
                   std::function<void(const std::string &)> reloadFn);

    /**
     * @brief Poll all watched files and fire callbacks for any that changed.
     *
     * Call once per frame (e.g. from a FrameUpdateSignal subscriber).
     *
     * @return @c true if at least one file changed.
     */
    bool poll();

    /**
     * @brief Number of currently registered meshes.
     */
    [[nodiscard]] std::size_t meshCount() const;

    /**
     * @brief Number of files being watched.
     */
    [[nodiscard]] std::size_t watchedFileCount() const;

private:
    struct Slot {
        MeshEntry                           entry;
        std::string                         sourcePath;
        std::shared_ptr<std::atomic<bool>>  valid =
            std::make_shared<std::atomic<bool>>(true);
    };

    struct WatchEntry {
        std::filesystem::file_time_type             baseline{};
        std::vector<std::function<void(const std::string &)>> callbacks;
    };

    VMAAllocator *allocator_ = nullptr;
    GPUDevice    *device_    = nullptr;

    mutable std::shared_mutex slotsMutex_;
    uint64_t                   nextId_ = 1;
    std::unordered_map<uint64_t, Slot> slots_;

    mutable std::mutex                              watchMutex_;
    std::unordered_map<std::string, WatchEntry>     watches_;
};

} // namespace device

#endif // MESH_REGISTRY_H_
