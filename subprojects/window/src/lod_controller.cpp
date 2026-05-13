#include "lod_controller.h"
#include "object.h"
#include <cmath>
#include <glm/geometric.hpp>

namespace window {

void LODSystem::update(LODController                           &lod,
                       const glm::vec3                         &entityPos,
                       const glm::vec3                         &cameraPos,
                       ecs::component::object::RenderComponent &rc) {
    if (lod.levels.empty()) {
        return;
    }

    const float distance = glm::distance(entityPos, cameraPos);

    // Levels are ordered finest-first.  Select the first level whose
    // maxDistance >= current distance.  Fall back to the last (coarsest) level
    // if the entity is beyond all specified thresholds.
    uint32_t selectedLevel = static_cast<uint32_t>(lod.levels.size()) - 1u;
    for (uint32_t i = 0; i < static_cast<uint32_t>(lod.levels.size()); ++i) {
        if (distance <= lod.levels[i].maxDistance) {
            selectedLevel = i;
            break;
        }
    }

    if (selectedLevel != lod.currentLevel) {
        lod.currentLevel = selectedLevel;
        rc.setMesh(lod.levels[selectedLevel].mesh);
    }
}

} // namespace window
