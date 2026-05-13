#ifndef RENDER_WORLD_H_
#define RENDER_WORLD_H_

#include "object.h"
#include "signal.hpp"
#include "vulkan/vulkan.hpp"
#include "window_export.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace window {

/**
 * @brief Per-frame update callback invoked just before a renderable is drawn.
 *
 * Receives the current frame-in-flight index so the callback can select the
 * right per-frame resource (e.g. uniform buffer).
 */
using RenderUpdateFunc = std::function<void(uint32_t frameIndex)>;

/**
 * @brief A flat, thread-safe list of renderables.
 *
 * RenderWorld decouples the rendering pass from scene lifecycle management.
 * Scenes (or any other subsystem) register their @ref RenderComponentBase
 * objects here; the renderer then calls @ref draw without
 * knowing anything about scenes.
 *
 * Entries are stored as lightweight descriptors (pointer + optional callback).
 * Ownership of the actual render components stays with whatever object created
 * them (e.g. an ECS entity inside a Scene).
 *
 * Thread-safety: all public methods that mutate or iterate the entry list take
 * an internal lock, so it is safe to call @ref add / @ref remove from any
 * thread while @ref draw is called from the render thread.
 */
class WINDOW_API RenderWorld {
public:
  RenderWorld() = default;
  ~RenderWorld() = default;

  // Non-copyable, non-moveable (Signal members are non-moveable)
  RenderWorld(const RenderWorld &) = delete;
  RenderWorld &operator=(const RenderWorld &) = delete;
  RenderWorld(RenderWorld &&) = delete;
  RenderWorld &operator=(RenderWorld &&) = delete;

  // ── Registration ───────────────────────────────────────────────────────

  /**
   * @brief Register a renderable.
   * @param rc         Non-owning pointer to the render component.
   * @param updateFunc Optional callback invoked once per frame (before draw).
   *
   * If @p rc is already registered it is silently ignored (no duplicates).
   */
  void add(ecs::component::object::RenderComponentBase *rc,
           RenderUpdateFunc updateFunc = {});

  /**
   * @brief Unregister a previously registered renderable.
   *
   * No-op if @p rc is not found.
   */
  void remove(const ecs::component::object::RenderComponentBase *rc);

  /**
   * @brief Remove all registered renderables.
   */
  void clear();

  // ── Rendering ──────────────────────────────────────────────────────────

  /**
   * @brief Issue draw commands for all registered renderables.
   *
   * For each entry the optional update callback is called first, then
   * @ref RenderComponentBase::draw is invoked.
   */
  void draw(vk::CommandBuffer cmd, uint32_t frameIndex) const;

  // ── Signals ────────────────────────────────────────────────────────────

  /**
   * @brief Emitted after a renderable is added to the world.
   */
  core::signal::Signal<void(
      const ecs::component::object::RenderComponentBase *)>
      objectAdded;

  /**
   * @brief Emitted after a renderable is removed from the world.
   */
  core::signal::Signal<void(
      const ecs::component::object::RenderComponentBase *)>
      objectRemoved;

private:
  struct Entry {
    ecs::component::object::RenderComponentBase *renderComp = nullptr;
    RenderUpdateFunc update;

    Entry() = default;
    Entry(ecs::component::object::RenderComponentBase *rc,
          RenderUpdateFunc fn)
        : renderComp(rc), update(std::move(fn)) {}
  };

  std::vector<Entry> entries_;
  mutable std::shared_mutex mutex_;
};

} // namespace window

#endif // RENDER_WORLD_H_
