#ifndef HITBOX_H_
#define HITBOX_H_

#include "component_registry.h"
#include "mesh.h"
#include "window_export.h"
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

namespace ecs::component::object {

/**
 * @brief Axis-aligned bounding box component for physics / trigger queries.
 *
 * Attach a @c Hitbox alongside a @ref TransformComponent on any entity to
 * declare a collidable volume.  The component self-registers in the global
 * @ref ecs::component::ComponentRegistry<Hitbox> list on construction and
 * deregisters on destruction, enabling O(n) iteration over **all** live
 * hitboxes across all scenes without going through ECS entity storage:
 *
 * @code
 *   // Query every live Hitbox from any thread:
 *   Hitbox::forEach([&](Hitbox &hb) {
 *       // hb.bounds is in local (object) space.
 *       // Combine with entity transform to get world-space AABB.
 *   });
 *
 *   // Safe removal during iteration — use snapshot() first:
 *   auto ptrs = Hitbox::snapshot();
 *   for (Hitbox *p : ptrs) { maybeRemove(*p); }
 * @endcode
 *
 * ### World-space AABB
 * @c bounds stores the volume in **local (object) space**.  To compute the
 * world-space AABB, transform all 8 corners with the entity's model matrix
 * and re-fit:
 *
 * @code
 *   Hitbox::worldBounds(hb.bounds, transform.modelMatrix(), worldMin, worldMax);
 * @endcode
 */
struct WINDOW_API Hitbox : public ecs::component::ComponentRegistry<Hitbox> {
    /// Collision volume in local (object) space.
    BoundingBox bounds;

    // ── Constructors ──────────────────────────────────────────────────────

    Hitbox() = default;

    explicit Hitbox(const BoundingBox &b) : bounds(b) {}

    Hitbox(const glm::vec3 &min, const glm::vec3 &max)
        : bounds{min, max} {}

    // ── Utility ───────────────────────────────────────────────────────────

    /**
     * @brief Compute the world-space AABB by transforming all 8 corners.
     *
     * @param localBounds  AABB in local space.
     * @param modelMatrix  Entity model matrix (world transform).
     * @param outMin       Receives the world-space minimum corner.
     * @param outMax       Receives the world-space maximum corner.
     */
    static void worldBounds(const BoundingBox &localBounds,
                            const glm::mat4   &modelMatrix,
                            glm::vec3         &outMin,
                            glm::vec3         &outMax);
};

} // namespace ecs::component::object

#endif // HITBOX_H_
