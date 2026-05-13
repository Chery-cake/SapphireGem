#include "spatial_grid.h"

#include <algorithm>
#include <cmath>

namespace window {

// ── Construction ──────────────────────────────────────────────────────────────

SpatialGrid::SpatialGrid(float cellSize) noexcept
    : cellSize_(cellSize) {}

// ── Private helpers ───────────────────────────────────────────────────────────

int32_t SpatialGrid::toCell(float v) const noexcept {
    return static_cast<int32_t>(std::floor(v / cellSize_));
}

void SpatialGrid::cellRange(const ecs::component::object::BoundingBox &bounds,
                             CellKey                                    &outMin,
                             CellKey                                    &outMax) const noexcept {
    outMin = {toCell(bounds.min.x), toCell(bounds.min.y), toCell(bounds.min.z)};
    outMax = {toCell(bounds.max.x), toCell(bounds.max.y), toCell(bounds.max.z)};
}

void SpatialGrid::insertLocked(
    ecs::component::object::Hitbox                   *hb,
    const ecs::component::object::BoundingBox &worldBounds) {
    CellKey cMin, cMax;
    cellRange(worldBounds, cMin, cMax);

    for (int32_t x = cMin.x; x <= cMax.x; ++x) {
        for (int32_t y = cMin.y; y <= cMax.y; ++y) {
            for (int32_t z = cMin.z; z <= cMax.z; ++z) {
                cells_[{x, y, z}].push_back(hb);
            }
        }
    }
}

void SpatialGrid::removeLocked(
    ecs::component::object::Hitbox                   *hb,
    const ecs::component::object::BoundingBox &worldBounds) {
    CellKey cMin, cMax;
    cellRange(worldBounds, cMin, cMax);

    for (int32_t x = cMin.x; x <= cMax.x; ++x) {
        for (int32_t y = cMin.y; y <= cMax.y; ++y) {
            for (int32_t z = cMin.z; z <= cMax.z; ++z) {
                auto it = cells_.find({x, y, z});
                if (it == cells_.end()) {
                    continue;
                }
                auto &vec = it->second;
                vec.erase(std::ranges::remove(vec, hb).begin(), vec.end());
                if (vec.empty()) {
                    cells_.erase(it);
                }
            }
        }
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void SpatialGrid::insert(
    ecs::component::object::Hitbox                   *hb,
    const ecs::component::object::BoundingBox &worldBounds) {
    std::unique_lock lock(mutex_);
    insertLocked(hb, worldBounds);
}

void SpatialGrid::remove(
    ecs::component::object::Hitbox                   *hb,
    const ecs::component::object::BoundingBox &worldBounds) {
    std::unique_lock lock(mutex_);
    removeLocked(hb, worldBounds);
}

void SpatialGrid::update(
    ecs::component::object::Hitbox                   *hb,
    const ecs::component::object::BoundingBox &oldBounds,
    const ecs::component::object::BoundingBox &newBounds) {
    std::unique_lock lock(mutex_);
    removeLocked(hb, oldBounds);
    insertLocked(hb, newBounds);
}

void SpatialGrid::clear() {
    std::unique_lock lock(mutex_);
    cells_.clear();
}

std::vector<ecs::component::object::Hitbox *>
SpatialGrid::query(const ecs::component::object::BoundingBox &worldBounds) const {
    std::shared_lock lock(mutex_);

    CellKey cMin, cMax;
    cellRange(worldBounds, cMin, cMax);

    std::vector<ecs::component::object::Hitbox *> results;

    for (int32_t x = cMin.x; x <= cMax.x; ++x) {
        for (int32_t y = cMin.y; y <= cMax.y; ++y) {
            for (int32_t z = cMin.z; z <= cMax.z; ++z) {
                auto it = cells_.find({x, y, z});
                if (it == cells_.end()) {
                    continue;
                }
                for (auto *hb : it->second) {
                    results.push_back(hb);
                }
            }
        }
    }

    // Deduplicate: a hitbox spanning multiple cells appears only once.
    std::ranges::sort(results);
    results.erase(std::ranges::unique(results).begin(), results.end());

    return results;
}

std::size_t SpatialGrid::cellCount() const {
    std::shared_lock lock(mutex_);
    return cells_.size();
}

} // namespace window
