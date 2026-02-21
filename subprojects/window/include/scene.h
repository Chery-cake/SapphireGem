#ifndef SCENE_H_
#define SCENE_H_

#include "vulkan/vulkan.hpp"
#include "window_export.h"
#include <cstdint>
#include <mutex>
#include <vector>

// Forward declarations
namespace device {
class GPUDevice;
class VMAAllocator;
class ShaderManager;
} // namespace device

namespace window {

// Forward declaration
class Renderer;

/**
 * @brief Tag for identifying scenes in the resource system
 *
 * Must have static storage duration (constexpr, static, or global)
 * when used with ResourceRegistry.
 */
struct WINDOW_API SceneTag {
  const char *name;

  constexpr SceneTag(const char *n) : name(n) {}
};

/**
 * @brief Abstract base class for scenes
 *
 * A scene manages a collection of objects and their rendering.
 * Scenes can be loaded/unloaded from GPU memory to support
 * multiple scenes per window with memory-efficient switching.
 *
 * Subclasses implement the lifecycle methods to create and manage
 * their specific resources (materials, objects, textures).
 *
 * A window can display multiple scenes at once. When a scene is
 * not being presented, it is unloaded from GPU memory.
 *
 * Scenes are managed through a ResourceRegistry<SceneTag, Scene>
 * using the tag system for type-safe identification.
 *
 * Thread-safe: the loaded state is protected by mutex.
 */
class WINDOW_API Scene {
public:
  explicit Scene(const SceneTag &tag);
  virtual ~Scene();

  // Disable copy
  Scene(const Scene &) = delete;
  Scene &operator=(const Scene &) = delete;

  // Enable move
  Scene(Scene &&other) noexcept;
  Scene &operator=(Scene &&other) noexcept;

  /**
   * @brief Load scene resources onto GPU
   *
   * Called when the scene becomes active. Implementations should
   * create materials, objects, pipelines, and other GPU resources.
   *
   * Supports multi-GPU by receiving primary and secondary devices.
   *
   * @param device Primary GPU device
   * @param secondaryGPUs Secondary GPUs for multi-GPU rendering
   * @param allocator VMA allocator for buffer/image creation
   * @param shaderManager Shader manager for shader compilation
   * @param renderer Renderer providing render pass and frame info
   * @return true if loading succeeded
   */
  virtual bool load(device::GPUDevice &device,
                    std::vector<device::GPUDevice *> &secondaryGPUs,
                    device::VMAAllocator &allocator,
                    device::ShaderManager &shaderManager,
                    Renderer &renderer) = 0;

  /**
   * @brief Unload scene resources from GPU memory
   *
   * Called when the scene becomes inactive. Implementations should
   * release all GPU resources to free memory.
   */
  virtual void unload() = 0;

  /**
   * @brief Update scene state
   * @param deltaTime Time elapsed since last update in seconds
   */
  virtual void update(float deltaTime) = 0;

  /**
   * @brief Record draw commands for this scene
   *
   * Called within an active render pass. Implementations should
   * bind pipelines, descriptor sets, and issue draw calls.
   *
   * Supports multi-GPU by accepting a command buffer that may
   * be associated with any device queue.
   *
   * @param cmd Command buffer to record commands into
   * @param frameIndex Current frame-in-flight index
   */
  virtual void draw(vk::CommandBuffer cmd, uint32_t frameIndex) = 0;

  /**
   * @brief Check if scene resources are loaded
   * @return true if scene is loaded onto GPU
   */
  [[nodiscard]] bool isLoaded() const { return loaded_; }

  /**
   * @brief Get scene name
   * @return Scene name from tag
   */
  [[nodiscard]] const char *getName() const { return name_; }

protected:
  /**
   * @brief Set the loaded state
   * @param loaded New loaded state
   */
  void setLoaded(bool loaded) { loaded_ = loaded; }

private:
  const char *name_;
  bool loaded_ = false;
  mutable std::mutex sceneMutex_;
};

} // namespace window

#endif // SCENE_H_
