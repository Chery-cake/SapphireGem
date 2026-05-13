#ifndef LOD_CONTROLLER_H_
#define LOD_CONTROLLER_H_

#include "mesh.h"
#include "window_export.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace ecs::component::object {
struct RenderComponent;
} // namespace ecs::component::object

namespace window {

/**
 * @brief A single LOD level: the mesh geometry and the maximum view distance
 *        at which this level is active.
 *
 * Levels in @ref LODController::levels are ordered finest first (lowest index)
 * to coarsest last (highest index).  @c maxDistance is the furthest camera
 * distance at which this level is displayed.  The last level is the fallback
 * used when the distance exceeds all earlier thresholds:
 *
 * @code
 *   LODLevel{&meshHigh, 10.0f}  // finest — used when camera distance ≤ 10 m
 *   LODLevel{&meshMid,  40.0f}  // medium — used when camera distance ≤ 40 m
 *   LODLevel{&meshLow,   0.0f}  // coarsest — fallback for distance > 40 m
 * @endcode
 */
struct WINDOW_API LODLevel {
    const ecs::component::object::Mesh *mesh        = nullptr; ///< Geometry for this LOD
    float                               maxDistance = 0.0f;    ///< Max camera-distance at which this level is active.  The last level in the array acts as an unconditional fallback regardless of this value.
};

/**
 * @brief Component that drives Level-Of-Detail mesh switching for an entity.
 *
 * The @ref LODSystem selects the appropriate level each frame based on the
 * distance from the entity to the camera and calls
 * @ref ecs::component::object::RenderComponent::setMesh to swap the active mesh.
 *
 * Typical setup (finest → coarsest):
 * @code
 *   LODController lod;
 *   lod.levels.push_back({ &meshHigh,  10.0f }); // finest
 *   lod.levels.push_back({ &meshMid,   40.0f });
 *   lod.levels.push_back({ &meshLow,    0.0f }); // coarsest (fallback)
 * @endcode
 */
struct WINDOW_API LODController {
    /// LOD levels in order of increasing detail distance (finest first, coarsest last).
    std::vector<LODLevel> levels;

    /// Index of the currently active level (0 = finest).
    /// Managed by @ref LODSystem::update — do not modify directly.
    uint32_t currentLevel = 0;
};

/**
 * @brief Static helper that selects and applies the appropriate LOD level.
 *
 * @see LODController
 */
class WINDOW_API LODSystem {
public:
    /**
     * @brief Select the LOD level and swap the mesh in @p rc if needed.
     *
     * Algorithm:
     *  1. Compute the Euclidean distance between @p entityPos and @p cameraPos.
     *  2. Iterate @p lod.levels from index 0 upward.  Select the first level
     *     whose @c maxDistance is >= the computed distance.
     *  3. If no level matches (entity is very far), use the last level (coarsest
     *     fallback).
     *  4. If the selected level differs from @p lod.currentLevel, call
     *     @c rc.setMesh() and update @p lod.currentLevel.
     *
     * @param lod        The LOD controller component to update.
     * @param entityPos  World-space position of the entity.
     * @param cameraPos  World-space camera position.
     * @param rc         RenderComponent whose active mesh is swapped.
     */
    static void update(LODController                             &lod,
                       const glm::vec3                           &entityPos,
                       const glm::vec3                           &cameraPos,
                       ecs::component::object::RenderComponent   &rc);
};

} // namespace window

#endif // LOD_CONTROLLER_H_
