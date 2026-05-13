#include "shader_watcher.h"
#include "pipeline_cache.h"

#include <filesystem>
#include <print>

namespace window {

ShaderWatcher::ShaderWatcher(device::ShaderManager &shaderManager)
    : shaderManager_(shaderManager) {}

void ShaderWatcher::watchFile(const std::filesystem::path &path) {
    std::error_code ec;
    const auto      t = std::filesystem::last_write_time(path, ec);
    if (ec) {
        std::println("[ShaderWatcher] Could not stat '{}': {}",
                     path.string(), ec.message());
        return;
    }
    timestamps_[path.string()] = t;
}

void ShaderWatcher::watchDirectory(const std::filesystem::path &dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) {
        std::println("[ShaderWatcher] Directory '{}' not found — skipping.",
                     dir.string());
        return;
    }

    for (auto &entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (ec) {
            break; // stop on iteration error
        }
        if (entry.is_regular_file() && entry.path().extension() == ".slang") {
            watchFile(entry.path());
        }
    }
}

bool ShaderWatcher::poll() {
    if (timestamps_.empty()) {
        return false;
    }

    bool changed = false;
    for (auto &[pathStr, baseline] : timestamps_) {
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(pathStr, ec);
        if (!ec && t != baseline) {
            changed  = true;
            baseline = t;
            std::println("[ShaderWatcher] '{}' changed — scheduling reload.", pathStr);
        }
    }

    if (changed) {
        shaderManager_.clearCache();
        PipelineCache::instance().clear();
        std::println("[ShaderWatcher] Shader and pipeline caches cleared.");
    }

    return changed;
}

} // namespace window
