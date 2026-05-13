#include "pipeline_cache.h"
#include "material.h"
#include "shader_manager.h"
#include <print>

namespace window {

// ---------- Invalidate signal ----------
core::signal::Signal<void()> &PipelineCache::getInvalidateSignal() {
  static core::signal::Signal<void()> signal;
  return signal;
}

// ---------- LayoutKey helpers ----------

bool PipelineCache::LayoutKey::operator==(const LayoutKey &o) const {
  return set0Layout == o.set0Layout && bindlessLayout == o.bindlessLayout &&
         pushConstantSize == o.pushConstantSize &&
         pushConstantStages == o.pushConstantStages;
}

std::size_t
PipelineCache::LayoutKeyHash::operator()(const LayoutKey &key) const {
  std::size_t seed = 0;
  auto combine = [&](auto val) {
    std::hash<std::decay_t<decltype(val)>> h;
    seed ^= h(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  };
  combine(static_cast<VkDescriptorSetLayout>(key.set0Layout));
  combine(static_cast<VkDescriptorSetLayout>(key.bindlessLayout));
  combine(key.pushConstantSize);
  combine(static_cast<VkShaderStageFlags>(key.pushConstantStages));
  return seed;
}

// ---------- CacheKey helpers ----------

bool PipelineCache::CacheKey::operator==(const CacheKey &other) const {
  return shaderProgramHash == other.shaderProgramHash &&
         set0Layout == other.set0Layout && renderPass == other.renderPass &&
         config.topology == other.config.topology &&
         config.polygonMode == other.config.polygonMode &&
         config.cullMode == other.config.cullMode &&
         config.frontFace == other.config.frontFace &&
         config.depthTestEnable == other.config.depthTestEnable &&
         config.depthWriteEnable == other.config.depthWriteEnable &&
         config.blendEnable == other.config.blendEnable &&
         config.lineWidth == other.config.lineWidth &&
         config.pushConstantSize == other.config.pushConstantSize &&
         config.pushConstantStages == other.config.pushConstantStages &&
         textureCount == other.textureCount &&
         bindlessLayout == other.bindlessLayout &&
         dimension == other.dimension;
}

std::size_t PipelineCache::CacheKeyHash::operator()(const CacheKey &key) const {
  std::size_t seed = 0;
  auto combine = [&](auto val) {
    std::hash<std::decay_t<decltype(val)>> h;
    seed ^= h(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  };

  combine(key.shaderProgramHash);
  combine(static_cast<VkDescriptorSetLayout>(key.set0Layout));
  combine(static_cast<VkRenderPass>(key.renderPass));
  combine(static_cast<uint32_t>(key.config.topology));
  combine(static_cast<uint32_t>(key.config.polygonMode));
  combine(static_cast<VkCullModeFlags>(key.config.cullMode));
  combine(static_cast<VkFrontFace>(key.config.frontFace));
  combine(key.config.depthTestEnable);
  combine(key.config.depthWriteEnable);
  combine(key.config.blendEnable);
  combine(key.config.lineWidth);
  combine(key.config.pushConstantSize);
  combine(static_cast<VkShaderStageFlags>(key.config.pushConstantStages));
  combine(key.textureCount);
  combine(static_cast<VkDescriptorSetLayout>(key.bindlessLayout));
  combine(key.dimension);
  return seed;
}

// ---------- Singleton ----------

PipelineCache &PipelineCache::instance() {
  static PipelineCache inst;
  return inst;
}

// ---------- Layout cache ----------

std::shared_ptr<vk::raii::PipelineLayout>
PipelineCache::getOrCreateLayout(device::GPUDevice &device,
                                  const LayoutKey &key) {
  // Fast path
  {
    std::scoped_lock lock(mutex_);
    auto it = layoutCache_.find(key);
    if (it != layoutCache_.end())
      return it->second;
  }

  // Slow path: build a minimal PipelineConfig with only the layout-relevant
  // fields set, then delegate to Material::createPipelineLayout.
  PipelineConfig minimalConfig;
  minimalConfig.pushConstantSize = key.pushConstantSize;
  minimalConfig.pushConstantStages = key.pushConstantStages;

  auto layout = Material::createPipelineLayout(device, key.set0Layout,
                                               minimalConfig, key.bindlessLayout);
  if (!layout)
    return nullptr;

  {
    std::scoped_lock lock(mutex_);
    auto [it, inserted] = layoutCache_.try_emplace(key, std::move(layout));
    if (inserted) {
      std::println("[PipelineCache] Cached new pipeline layout (total {})",
                   layoutCache_.size());
    }
    return it->second;
  }
}

// ---------- Public API ----------

std::shared_ptr<ObjectPipeline>
PipelineCache::getOrCreate(device::GPUDevice &device, vk::RenderPass renderPass,
                           vk::DescriptorSetLayout set0Layout,
                           const PipelineConfig &config, uint32_t textureCount,
                           vk::DescriptorSetLayout bindlessLayout,
                           const device::ShaderProgram *shaderProgram) {
  if (!shaderProgram || !shaderProgram->compiled) {
    std::println(stderr, "[PipelineCache] Shader not compiled");
    return {};
  }

  CacheKey key;
  key.shaderProgramHash =
      shaderProgram->getHash(); // requires getHash() in ShaderProgram
  key.set0Layout = set0Layout;
  key.renderPass = renderPass;
  key.config = config;
  key.textureCount = textureCount;
  key.bindlessLayout = bindlessLayout;
  key.dimension = config.dimension; // pipeline is unique per dimension

  // Fast path: cache hit
  {
    std::scoped_lock lock(mutex_);
    auto it = cache_.find(key);
    if (it != cache_.end())
      return it->second;
  }

  // Slow path: create pipeline (outside lock to avoid blocking)
  auto stages = shaderProgram->getStageInfos();
  if (stages.empty()) {
    std::println(stderr, "[PipelineCache] No valid stages");
    return {};
  }

  ObjectPipeline newPipeline;
  // Look up (or create) a shared pipeline layout from the layout sub-cache.
  LayoutKey layoutKey;
  layoutKey.set0Layout = set0Layout;
  layoutKey.bindlessLayout = bindlessLayout;
  layoutKey.pushConstantSize = config.pushConstantSize;
  layoutKey.pushConstantStages = config.pushConstantStages;

  newPipeline.pipelineLayout = getOrCreateLayout(device, layoutKey);
  if (!newPipeline.pipelineLayout) {
    std::println(stderr, "[PipelineCache] Pipeline layout creation failed");
    return {};
  }
  if (!Material::createPipeline(device, renderPass, config, stages,
                                newPipeline)) {
    std::println(stderr, "[PipelineCache] Pipeline creation failed");
    return {};
  }

  auto shared = std::make_shared<ObjectPipeline>(std::move(newPipeline));

  // Insert into cache; if another thread did it meanwhile, return the
  // existing one.
  {
    std::scoped_lock lock(mutex_);
    auto [it, inserted] = cache_.try_emplace(std::move(key), shared);
    if (!inserted) {
      shared = it->second;
    } else {
      std::println("[PipelineCache] Cached new pipeline (total {})",
                   cache_.size());
    }
  }

  return shared;
}

void PipelineCache::clear() {

  getInvalidateSignal().emit();

  {
    std::scoped_lock lock(mutex_);
    cache_.clear();
    layoutCache_.clear();
  }
}

} // namespace window
