#include "hitbox.h"

#include <algorithm>
#include <glm/vec4.hpp>

namespace ecs::component::object {

void Hitbox::worldBounds(const BoundingBox &localBounds,
                         const glm::mat4   &modelMatrix,
                         glm::vec3         &outMin,
                         glm::vec3         &outMax) {
    const glm::vec3 &lo = localBounds.min;
    const glm::vec3 &hi = localBounds.max;

    // All 8 corners of the local AABB.
    const glm::vec4 corners[8] = {
        {lo.x, lo.y, lo.z, 1.f},
        {hi.x, lo.y, lo.z, 1.f},
        {lo.x, hi.y, lo.z, 1.f},
        {hi.x, hi.y, lo.z, 1.f},
        {lo.x, lo.y, hi.z, 1.f},
        {hi.x, lo.y, hi.z, 1.f},
        {lo.x, hi.y, hi.z, 1.f},
        {hi.x, hi.y, hi.z, 1.f},
    };

    glm::vec3 wMin{std::numeric_limits<float>::max()};
    glm::vec3 wMax{std::numeric_limits<float>::lowest()};

    for (const auto &c : corners) {
        const glm::vec3 wc = glm::vec3(modelMatrix * c);
        wMin = glm::min(wMin, wc);
        wMax = glm::max(wMax, wc);
    }

    outMin = wMin;
    outMax = wMax;
}

} // namespace ecs::component::object
