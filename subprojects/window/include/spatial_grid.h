#ifndef SPATIAL_GRID_H_
#define SPATIAL_GRID_H_

#include "hitbox.h"
#include "mesh.h"
#include "window_export.h"
#include <cstdint>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace window {

/**
 * @brief Integer 3-D cell coordinate used as the key in @ref SpatialGrid.
 *
 * Each component is the floor of the world coordinate divided by the grid's
 * @c cellSize, so negative coordinates are handled correctly.
 */
struct WINDOW_API CellKey {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    bool operator==(const CellKey &) const noexcept = default;
};

} // namespace window

// ── std::hash specialization ─────────────────────────────────────────────────

namespace std {

template <> struct hash<window::CellKey> {
    [[nodiscard]] std::size_t operator()(const window::CellKey &k) const noexcept {
        // FNV-1a–inspired mix to keep the hash well-distributed.
        std::size_t h = 2166136261u;
        h ^= static_cast<std::size_t>(k.x);
        h *= 16777619u;
        h ^= static_cast<std::size_t>(k.y);
        h *= 16777619u;
        h ^= static_cast<std::size_t>(k.z);
        h *= 16777619u;
        return h;
    }
};

} // namespace std

namespace window {

/**
 * @brief Thread-safe uniform spatial grid for broad-phase collision queries.
 *
 * The world is divided into axis-aligned cubes of side @p cellSize.  Each
 * @ref ecs::component::object::Hitbox is mapped into every cell whose AABB
 * overlaps the hitbox's world-space bounding box.  A query returns all
 * hitboxes that are registered in at least one cell that overlaps the query
 * AABB — callers should then perform narrow-phase testing on the candidates.
 *
 * ### Thread-safety
 * All public mutating operations (@ref insert, @ref remove, @ref update,
 * @ref clear) acquire an *exclusive* lock.  @ref query acquires a *shared*
 * lock and may run concurrently with other queries, but not with mutations.
 *
 * ### Usage
 * @code
 *   window::SpatialGrid grid{2.0f};           // 2-unit cells
 *
 *   // When an entity is created / moved:
 *   glm::vec3 wMin, wMax;
 *   ecs::component::object::Hitbox::worldBounds(
 *       hb.bounds, transform.modelMatrix(), wMin, wMax);
 *   ecs::component::object::BoundingBox wb{wMin, wMax};
 *   grid.insert(&hb, wb);
 *
 *   // Query candidates for a volume:
 *   auto candidates = grid.query(queryBounds);
 *   for (auto *c : candidates) { narrowPhaseTest(*c); }
 *
 *   // Remove when entity is destroyed:
 *   grid.remove(&hb, wb);
 * @endcode
 */
class WINDOW_API SpatialGrid {
public:
    // ── Constructors ─────────────────────────────────────────────────────────

    /**
     * @brief Construct a spatial grid with the given uniform cell size.
     * @param cellSize  Side length of each cubic cell (must be > 0).
     */
    explicit SpatialGrid(float cellSize) noexcept;

    ~SpatialGrid() = default;

    // Non-copyable (mutex is non-copyable); moveable if needed in the future.
    SpatialGrid(const SpatialGrid &) = delete;
    SpatialGrid &operator=(const SpatialGrid &) = delete;
    SpatialGrid(SpatialGrid &&) = delete;
    SpatialGrid &operator=(SpatialGrid &&) = delete;

    // ── Configuration ─────────────────────────────────────────────────────

    /// Return the cell size this grid was constructed with.
    [[nodiscard]] float cellSize() const noexcept { return cellSize_; }

    // ── Mutation ──────────────────────────────────────────────────────────

    /**
     * @brief Register a hitbox in all cells overlapped by @p worldBounds.
     *
     * If the hitbox is already registered it may appear multiple times in a
     * cell; callers should call @ref remove before re-inserting after a move
     * (or use @ref update).
     *
     * @param hb          Hitbox to register.  Must remain alive until removed.
     * @param worldBounds World-space AABB that covers the hitbox volume.
     */
    void insert(ecs::component::object::Hitbox                   *hb,
                const ecs::component::object::BoundingBox &worldBounds);

    /**
     * @brief Unregister a hitbox from all cells overlapped by @p worldBounds.
     *
     * @p worldBounds must describe the *same* volume that was used when
     * @ref insert was called for @p hb.
     *
     * No-op for cells that do not contain @p hb.
     */
    void remove(ecs::component::object::Hitbox                   *hb,
                const ecs::component::object::BoundingBox &worldBounds);

    /**
     * @brief Move a hitbox from its old cells to its new cells.
     *
     * Equivalent to `remove(hb, oldBounds); insert(hb, newBounds);` but
     * acquires the mutex only once.
     *
     * @param hb        Hitbox to update.
     * @param oldBounds World-space AABB used during the last @ref insert.
     * @param newBounds New world-space AABB after the entity moved.
     */
    void update(ecs::component::object::Hitbox                   *hb,
                const ecs::component::object::BoundingBox &oldBounds,
                const ecs::component::object::BoundingBox &newBounds);

    /**
     * @brief Remove all entries from the grid.
     */
    void clear();

    // ── Query ─────────────────────────────────────────────────────────────

    /**
     * @brief Return all hitboxes whose cells overlap @p worldBounds.
     *
     * The result is a deduplicated set of pointers — hitboxes that span
     * multiple cells appear only once.  Callers should perform narrow-phase
     * testing to eliminate false positives introduced by the broad-phase cell
     * overlap.
     *
     * @param worldBounds  World-space AABB of the query volume.
     * @return             Candidate hitbox pointers (unordered).
     */
    [[nodiscard]] std::vector<ecs::component::object::Hitbox *>
    query(const ecs::component::object::BoundingBox &worldBounds) const;

    /**
     * @brief Return the number of occupied cells.
     */
    [[nodiscard]] std::size_t cellCount() const;

private:
    float cellSize_;

    std::unordered_map<CellKey, std::vector<ecs::component::object::Hitbox *>>
        cells_;

    mutable std::shared_mutex mutex_;

    // ── Helpers ─────────────────────────────────────────────────────────

    /// Convert a world-space coordinate to a cell index along one axis.
    [[nodiscard]] int32_t toCell(float v) const noexcept;

    /// Compute the min and max cell indices that cover @p bounds.
    void cellRange(const ecs::component::object::BoundingBox &bounds,
                   CellKey &outMin,
                   CellKey &outMax) const noexcept;

    /// Insert into cells — called under exclusive lock.
    void insertLocked(ecs::component::object::Hitbox                   *hb,
                      const ecs::component::object::BoundingBox &worldBounds);

    /// Remove from cells — called under exclusive lock.
    void removeLocked(ecs::component::object::Hitbox                   *hb,
                      const ecs::component::object::BoundingBox &worldBounds);
};

} // namespace window

#endif // SPATIAL_GRID_H_
