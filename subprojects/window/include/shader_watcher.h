#ifndef SHADER_WATCHER_H_
#define SHADER_WATCHER_H_

#include "pipeline_cache.h"
#include "shader_manager.h"
#include "window_export.h"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace window {

/**
 * @brief Polls shader source files for modifications and triggers hot-reload.
 *
 * Add one or more shader source files (or a directory of @c *.slang files)
 * to watch.  Each call to @ref poll compares the current
 * @c std::filesystem::last_write_time against the stored baseline.  If any
 * file has changed, the ShaderManager cache and the PipelineCache are both
 * cleared, making every subsequent pipeline-creation request recompile from
 * source.
 *
 * The watcher does **not** recompile shaders itself — callers are expected to
 * do so by re-calling @ref ShaderManager::acquire for their tags after @ref
 * poll returns @c true.
 *
 * ### Integration with FrameUpdateSignal
 * @code
 *   ShaderWatcher watcher{shaderManager};
 *   watcher.watchDirectory("assets/shaders");
 *
 *   frameUpdateSignal.connect([&](float, uint32_t) {
 *       if (watcher.poll()) {
 *           // Re-acquire all shader programs used by this scene.
 *           shaderManager.acquire(&MY_SHADER_TAG);
 *       }
 *   });
 * @endcode
 */
class WINDOW_API ShaderWatcher {
public:
    /**
     * @brief Construct a ShaderWatcher bound to the given ShaderManager.
     * @param shaderManager  Must outlive this watcher.
     */
    explicit ShaderWatcher(device::ShaderManager &shaderManager);

    ~ShaderWatcher() = default;

    // Non-copyable, non-moveable (holds a reference to ShaderManager).
    ShaderWatcher(const ShaderWatcher &) = delete;
    ShaderWatcher &operator=(const ShaderWatcher &) = delete;
    ShaderWatcher(ShaderWatcher &&) = delete;
    ShaderWatcher &operator=(ShaderWatcher &&) = delete;

    // ── Configuration ─────────────────────────────────────────────────────

    /**
     * @brief Watch a single shader source file.
     *
     * The file is added to the watch list and its last-write-time is
     * recorded immediately as the baseline.
     *
     * @param path  Path to a @c .slang (or any text) source file.
     */
    void watchFile(const std::filesystem::path &path);

    /**
     * @brief Recursively watch all @c *.slang files in a directory.
     *
     * @param dir  Directory to scan.  Non-existent paths are silently ignored.
     */
    void watchDirectory(const std::filesystem::path &dir);

    // ── Per-frame API ──────────────────────────────────────────────────────

    /**
     * @brief Poll all watched files for modifications.
     *
     * If one or more files have changed since the last call (or since the
     * watcher was constructed), both @ref ShaderManager::clearCache and
     * @ref PipelineCache::clear are called and the baselines are updated.
     *
     * @return @c true if at least one file changed and caches were cleared.
     */
    bool poll();

    /**
     * @brief Number of files currently being watched.
     */
    [[nodiscard]] std::size_t watchedFileCount() const {
        return timestamps_.size();
    }

private:
    device::ShaderManager &shaderManager_;

    /// Stores last known modification time for each watched path.
    std::unordered_map<std::string,
                       std::filesystem::file_time_type> timestamps_;
};

} // namespace window

#endif // SHADER_WATCHER_H_
