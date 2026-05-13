#ifndef SCENE_H_
#define SCENE_H_

#include "async_compute_manager.h"
#include "frame_update_signal.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "object.h"
#include "render_world.h"
#include "signal.hpp"
#include "vulkan/vulkan.hpp"
#include "window_export.h"
#include <cstdint>
#include <functional>
#include <memory>
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
class ObjectBase;

/**
 * @brief A user callback called before the object's draw.
 *        Use it to update uniforms, transform, etc.
 */
using ObjectUpdateFunc = std::function<void(uint32_t frameIndex)>;

// For 3D scenes
struct Scene3DFrameData {
  glm::mat4 view{1.0f};
  glm::mat4 proj{1.0f};
};

// For 2D scenes
struct Scene2DFrameData {
  glm::mat3 view{1.0f};
  glm::mat3 proj{1.0f};
};

// TODO improve object draw updates with signals

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
 * Thread-safe: entity management and rendering are delegated to a
 * @ref RenderWorld whose internal mutex provides the necessary
 * thread-safety guarantees.
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

  // ----- Lifecycle (can be overridden) -----
  virtual bool load(device::GPUDevice &device,
                    std::vector<device::GPUDevice *> &secondaryGPUs,
                    device::VMAAllocator &allocator,
                    device::ShaderManager &shaderManager, Renderer &renderer);
  virtual void unload();
  virtual void update(float deltaTime);
  virtual void draw(vk::CommandBuffer cmd, uint32_t frameIndex);

  /**
   * @brief Called by Window after the scene is loaded and presented.
   *
   * Override to register compute effects with @p manager and to connect
   * frame-update subscribers to @p signal.  The base implementation is empty.
   *
   * @param manager  The window's AsyncComputeManager (never null when called).
   * @param signal   The window's FrameUpdateSignal (never null when called).
   */
  virtual void onComputeAttach(AsyncComputeManager *manager,
                               FrameUpdateSignal *signal);

  /**
   * @brief Called by Window before the scene is unpresented or destroyed.
   *
   * Override to unregister all effects previously registered in
   * @ref onComputeAttach.  The base implementation is empty.
   *
   * @param manager  The window's AsyncComputeManager (never null when called).
   */
  virtual void onComputeDetach(AsyncComputeManager *manager);

  // ----- Manage objects (thread‑safe) -----
  /**
   * @brief Add a renderable entity to the scene.
   * @param rc         Pointer to the render component (must outlive the
   * scene).
   * @param updateFunc Optional per‑frame update (e.g. for uniforms).
   */
  void addEntity(ecs::component::object::RenderComponentBase *rc,
                 ObjectUpdateFunc updateFunc = {});

  /**
   * @brief Remove an entity by its render component pointer.
   */
  void removeEntity(const ecs::component::object::RenderComponentBase *rc);
  void clearEntities();

  [[nodiscard]] bool isLoaded() const { return loaded_; }
  [[nodiscard]] const char *getName() const { return name_; }

  /**
   * @brief Signals forwarded from the internal RenderWorld.
   *
   * These are still emitted by the scene so external observers (e.g. the
   * window) can react to entity lifecycle changes without depending on
   * RenderWorld directly.
   */
  core::signal::Signal<void(
      const ecs::component::object::RenderComponentBase *)>
      objectAdded;
  core::signal::Signal<void(
      const ecs::component::object::RenderComponentBase *)>
      objectRemoved;

  /**
   * @brief Expose the underlying RenderWorld.
   *
   * Advanced callers (e.g. a multi-scene compositor) can use the
   * RenderWorld directly to submit additional renderables without going
   * through the Scene lifecycle.
   */
  [[nodiscard]] RenderWorld &getRenderWorld() { return *renderWorld_; }
  [[nodiscard]] const RenderWorld &getRenderWorld() const {
    return *renderWorld_;
  }

protected:
  void setLoaded(bool loaded) { loaded_ = loaded; }

private:
  std::unique_ptr<RenderWorld> renderWorld_;

  const char *name_;
  bool loaded_ = false;
  mutable std::mutex sceneMutex_;
};

} // namespace window

#endif // SCENE_H_
