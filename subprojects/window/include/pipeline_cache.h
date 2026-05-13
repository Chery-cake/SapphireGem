#ifndef PIPELINE_CACHE_H_
#define PIPELINE_CACHE_H_

#include "material.h"
#include "signal.hpp"
#include "vulkan/vulkan.hpp"
#include "window_export.h"
#include <functional>
#include <memory>

namespace window {

/**
 * @brief Global pipeline and pipeline‑layout cache.
 *
 * Pipelines are created lazily and shared among all objects. The cache key
 * includes everything that makes a pipeline unique. This enables future
 * draw‑call batching because you can find all objects using the same key.
 */
class WINDOW_API PipelineCache {
public:
  static PipelineCache &instance();

  PipelineCache(const PipelineCache &) = delete;
  PipelineCache &operator=(const PipelineCache &) = delete;

  /// Access the invalidate signal (tied to the lifetime of the cache).
  static core::signal::Signal<void()> &getInvalidateSignal();

  /**
   * @brief Get or create a pipeline for the given combination.
   *
   * @return Shared pointer (never null if all parameters are valid).
   */
  std::shared_ptr<ObjectPipeline>
  getOrCreate(device::GPUDevice &device, vk::RenderPass renderPass,
              vk::DescriptorSetLayout set0Layout, const PipelineConfig &config,
              uint32_t textureCount, vk::DescriptorSetLayout bindlessLayout,
              const device::ShaderProgram *shaderProgram);

  /// Clear all cached entries. Fires all registered signals afterwards.
  void clear();

private:
  PipelineCache() = default;
  ~PipelineCache() = default;

  struct CacheKey {
    std::size_t shaderProgramHash = 0;
    vk::DescriptorSetLayout set0Layout{};
    vk::RenderPass renderPass{};
    PipelineConfig config{};
    uint32_t textureCount = 0;
    vk::DescriptorSetLayout bindlessLayout{};
    uint32_t dimension = 0; ///< Spatial dimension; separates 2D and 3D pipelines

    bool operator==(const CacheKey &other) const;
  };
  struct CacheKeyHash {
    std::size_t operator()(const CacheKey &key) const;
  };

  mutable std::mutex mutex_;
  std::unordered_map<CacheKey, std::shared_ptr<ObjectPipeline>, CacheKeyHash>
      cache_;
};

} // namespace window

#endif // PIPELINE_CACHE_H_
