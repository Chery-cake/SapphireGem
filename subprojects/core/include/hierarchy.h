#ifndef HIERARCHY_H_
#define HIERARCHY_H_

#include "component_registry.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <future>
#include <functional>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ecs::component {

// ── EntityId ─────────────────────────────────────────────────────────────────

/// Opaque 64-bit entity identifier.  Zero is the sentinel "no entity" value.
using EntityId = uint64_t;

/// Sentinel: no parent / invalid entity.
inline constexpr EntityId kInvalidEntityId = 0;

// ── HierarchyNode ─────────────────────────────────────────────────────────────

/**
 * @brief Transform-hierarchy component.
 *
 * Attach a @c HierarchyNode to any entity to participate in the scene
 * hierarchy.  The @ref HierarchySystem walks the hierarchy top-down every
 * frame and writes the composed world transform into @ref worldTransform.
 *
 * ### Usage
 * @code
 *   // Create a child node at id=2, parented to id=1:
 *   HierarchyNode child;
 *   child.entityId    = 2;
 *   child.parentId    = 1;
 *   child.localTransform = glm::translate(glm::mat4{1.f}, {1.f, 0.f, 0.f});
 * @endcode
 *
 * @note  `worldTransform` is read-only outside the @ref HierarchySystem.
 *        Reading it before @ref HierarchySystem::update has been called for
 *        this frame will return last frame's value.
 */
struct HierarchyNode : public ComponentRegistry<HierarchyNode> {
    EntityId entityId  = kInvalidEntityId; ///< Identifies this node.
    EntityId parentId  = kInvalidEntityId; ///< Parent, or kInvalidEntityId for root.
    uint32_t depthHint = 0;                ///< Estimated depth (filled by system).

    /// Transform relative to the parent (or world if root).
    glm::mat4 localTransform{1.f};

    /// World-space composed transform.  Written by @ref HierarchySystem::update.
    glm::mat4 worldTransform{1.f};

    HierarchyNode() = default;
    explicit HierarchyNode(EntityId id,
                           EntityId parent = kInvalidEntityId,
                           const glm::mat4 &local = glm::mat4{1.f})
        : entityId(id), parentId(parent), localTransform(local) {}
};

// ── HierarchySystem ───────────────────────────────────────────────────────────

/**
 * @brief Top-down world-transform propagation system.
 *
 * Call @ref update once per frame (e.g. from a FrameUpdateSignal subscriber)
 * to propagate local transforms down the hierarchy.
 *
 * The algorithm is BFS from roots so that each parent is computed before its
 * children; levels at the same depth are independent and can be updated in
 * parallel (see @ref updateParallel).
 *
 * @code
 *   // In your scene's FrameUpdateSignal callback:
 *   HierarchySystem::update();
 * @endcode
 */
class HierarchySystem {
public:
    /**
     * @brief Single-threaded top-down world-transform update.
     *
     * Iterates all live @ref HierarchyNode instances registered with
     * @ref ComponentRegistry<HierarchyNode>, builds a parent-to-children
     * adjacency map, then propagates transforms from roots down.
     */
    static void update();

    /**
     * @brief Parallel world-transform update (level-by-level).
     *
     * Same as @ref update but processes each level of the hierarchy
     * concurrently using the supplied executor.
     *
     * @param executor  Called with `(fn)` for each independent subtask.
     *                  A typical implementation launches `std::async` or
     *                  submits to a thread pool.  `fn` takes no arguments.
     */
    template <typename Executor>
    static void updateParallel(Executor &&executor);

private:
    /// Build adjacency and fill per-node depth hints.  Returns ordered levels.
    static std::vector<std::vector<HierarchyNode *>>
    buildLevels(const std::vector<HierarchyNode *> &all,
                std::unordered_map<EntityId, HierarchyNode *> &idMap);
};

// ── StatsComponent ────────────────────────────────────────────────────────────

/**
 * @brief Per-entity performance-metrics component.
 *
 * Attach a @c StatsComponent to any entity and update the counters each
 * frame.  A debug overlay system can call @ref StatsComponent::forEach or
 * @ref StatsComponent::snapshot to gather totals across all live instances.
 *
 * All counters are atomic so they can be updated from worker threads
 * (e.g. a render thread) without external locking.
 *
 * @code
 *   // Record one frame's work:
 *   stats.drawCalls.fetch_add(numDrawCalls, std::memory_order_relaxed);
 *   stats.verticesRendered.fetch_add(numVerts, std::memory_order_relaxed);
 *
 *   // Reset at frame boundary:
 *   stats.reset();
 * @endcode
 */
struct StatsComponent : public ComponentRegistry<StatsComponent> {
    std::atomic<uint64_t> drawCalls{0};
    std::atomic<uint64_t> verticesRendered{0};
    /// GPU time in nanoseconds recorded via timestamp queries.
    std::atomic<uint64_t> gpuTimeNs{0};

    StatsComponent() = default;

    // Atomics are not moveable by default; provide explicit move so ECS
    // containers can relocate StatsComponent objects.
    StatsComponent(StatsComponent &&other) noexcept
        : drawCalls(other.drawCalls.load(std::memory_order_relaxed)),
          verticesRendered(other.verticesRendered.load(std::memory_order_relaxed)),
          gpuTimeNs(other.gpuTimeNs.load(std::memory_order_relaxed)) {}

    StatsComponent &operator=(StatsComponent &&other) noexcept {
        drawCalls.store(other.drawCalls.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
        verticesRendered.store(
            other.verticesRendered.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        gpuTimeNs.store(other.gpuTimeNs.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
        return *this;
    }

    /// Zero all counters (call once per frame boundary).
    void reset() noexcept {
        drawCalls.store(0, std::memory_order_relaxed);
        verticesRendered.store(0, std::memory_order_relaxed);
        gpuTimeNs.store(0, std::memory_order_relaxed);
    }

    /**
     * @brief Aggregate totals from all live StatsComponent instances.
     *
     * @return Snapshot with summed counters (not an atomic — for display
     *         purposes only).
     */
    struct Totals {
        uint64_t drawCalls       = 0;
        uint64_t verticesRendered = 0;
        uint64_t gpuTimeNs       = 0;
    };

    static Totals gatherTotals() {
        Totals t{};
        StatsComponent::forEachConst([&](const StatsComponent &s) {
            t.drawCalls        += s.drawCalls.load(std::memory_order_relaxed);
            t.verticesRendered += s.verticesRendered.load(std::memory_order_relaxed);
            t.gpuTimeNs        += s.gpuTimeNs.load(std::memory_order_relaxed);
        });
        return t;
    }
};

// ── AsyncLoadComponent ────────────────────────────────────────────────────────

/**
 * @brief Async-loading component for streaming resources.
 *
 * Hold a @c std::future<T> for a heavy resource (mesh, texture handle …).
 * Each frame, call @ref poll.  When the future is ready the supplied
 * @p onReady callback is invoked with the result value so the owner can
 * swap the placeholder with the real asset.
 *
 * @tparam T  Type produced by the background load (e.g. `Mesh`,
 *             `std::shared_ptr<Texture>`, `GeometryHandle`).
 *
 * ### Usage
 * @code
 *   AsyncLoadComponent<Mesh> loader;
 *   loader.startLoad(std::async(std::launch::async, loadMeshFromDisk, path));
 *   loader.onReady = [&](Mesh &&mesh) {
 *       entity.get<Mesh>() = std::move(mesh);
 *   };
 *
 *   // In the update loop:
 *   if (loader.poll()) {
 *       // onReady was called; component can now be removed.
 *   }
 * @endcode
 */
template <typename T>
struct AsyncLoadComponent {
    /// Starts loading: stores the future and marks this component as pending.
    void startLoad(std::future<T> fut) {
        future_      = std::move(fut);
        usePlaceholder_ = true;
        done_        = false;
    }

    /**
     * @brief Check if the future is ready and, if so, fire @p onReady.
     *
     * @return @c true if the future was ready this call (and @c onReady was
     *         invoked); @c false if still loading.
     */
    bool poll() {
        if (done_ || !future_.valid()) {
            return done_;
        }
        const auto status = future_.wait_for(std::chrono::seconds{0});
        // Both 'ready' and 'deferred' mean we can call get() without blocking:
        //  - ready:    the background task completed.
        //  - deferred: the task runs synchronously on the first get() call.
        if (status == std::future_status::ready ||
            status == std::future_status::deferred) {
            if (onReady) {
                onReady(future_.get());
            } else {
                future_.get(); // consume result even if no callback
            }
            usePlaceholder_ = false;
            done_           = true;
            return true;
        }
        return false;
    }

    /// True while the background load is still in-flight.
    [[nodiscard]] bool isLoading()     const noexcept { return !done_ && future_.valid(); }
    /// True after the load completed successfully.
    [[nodiscard]] bool isDone()        const noexcept { return done_; }
    /// True while the asset hasn't arrived yet (show placeholder).
    [[nodiscard]] bool usePlaceholder() const noexcept { return usePlaceholder_; }

    /// Called with the loaded value when the future becomes ready.
    std::function<void(T &&)> onReady;

private:
    std::future<T> future_;
    bool usePlaceholder_ = false;
    bool done_           = false;
};

// ── ObjectPool ────────────────────────────────────────────────────────────────

/**
 * @brief Fixed-capacity pre-allocated entity pool.
 *
 * Pre-creates @p Capacity instances of @p Entity.  @ref acquire returns a
 * pointer to an available entity (or @c nullptr if the pool is exhausted).
 * @ref release returns the entity to the pool after calling
 * `entity.resetComponents()` (if the entity has that method) or a
 * user-supplied reset lambda.
 *
 * @tparam Entity  Entity type.  May optionally provide `void resetComponents()`
 *                 or the caller can pass a reset function to @ref release.
 *
 * ### Usage
 * @code
 *   using Projectile = ecs::entity::Tuple<TransformComponent<3>, RenderComponent>;
 *   ObjectPool<Projectile, 256> pool;
 *
 *   // Spawn:
 *   auto *p = pool.acquire();
 *
 *   // Destroy:
 *   pool.release(p, [](Projectile &proj) {
 *       proj.get<TransformComponent<3>>() = {};
 *   });
 * @endcode
 */
template <typename Entity, std::size_t Capacity>
class ObjectPool {
public:
    ObjectPool() {
        available_.reserve(Capacity);
        for (auto &e : storage_) {
            available_.push_back(&e);
        }
    }

    // Non-copyable, non-moveable (pointers into storage_ would dangle).
    ObjectPool(const ObjectPool &) = delete;
    ObjectPool &operator=(const ObjectPool &) = delete;
    ObjectPool(ObjectPool &&) = delete;
    ObjectPool &operator=(ObjectPool &&) = delete;

    /**
     * @brief Acquire an entity from the pool.
     * @return Pointer to a free entity, or @c nullptr if exhausted.
     */
    Entity *acquire() {
        std::lock_guard lock(mutex_);
        if (available_.empty()) {
            return nullptr;
        }
        Entity *e = available_.back();
        available_.pop_back();
        return e;
    }

    /**
     * @brief Return an entity to the pool using a custom reset function.
     *
     * @param entity  Pointer previously returned by @ref acquire.
     * @param resetFn Called with `*entity` before it is returned.
     */
    template <typename ResetFn>
    void release(Entity *entity, ResetFn &&resetFn) {
        assert(entity != nullptr);
        std::lock_guard lock(mutex_);
        resetFn(*entity);
        available_.push_back(entity);
    }

    /**
     * @brief Return an entity to the pool, calling `entity.resetComponents()`
     *        if the method exists, otherwise no-op for the reset.
     *
     * @param entity  Pointer previously returned by @ref acquire.
     */
    void release(Entity *entity) {
        release(entity, [](Entity &e) {
            if constexpr (requires { e.resetComponents(); }) {
                e.resetComponents();
            }
        });
    }

    /// Number of entities currently available (not in use).
    [[nodiscard]] std::size_t available() const {
        std::lock_guard lock(mutex_);
        return available_.size();
    }

    /// Total pool capacity.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

private:
    std::array<Entity, Capacity> storage_;
    std::vector<Entity *>        available_;
    mutable std::mutex           mutex_;
};

// ── HierarchySystem implementation ───────────────────────────────────────────

inline std::vector<std::vector<HierarchyNode *>>
HierarchySystem::buildLevels(
    const std::vector<HierarchyNode *>           &all,
    std::unordered_map<EntityId, HierarchyNode *> &idMap) {
    idMap.clear();
    idMap.reserve(all.size());
    for (HierarchyNode *n : all) {
        if (n->entityId != kInvalidEntityId) {
            idMap[n->entityId] = n;
        }
    }

    // BFS from roots
    std::vector<std::vector<HierarchyNode *>> levels;
    std::vector<HierarchyNode *> current;

    for (HierarchyNode *n : all) {
        bool isRoot = (n->parentId == kInvalidEntityId) ||
                      (idMap.find(n->parentId) == idMap.end());
        if (isRoot) {
            current.push_back(n);
            n->depthHint = 0;
        }
    }

    while (!current.empty()) {
        levels.push_back(current);
        std::vector<HierarchyNode *> next;
        for (HierarchyNode *parent : current) {
            for (HierarchyNode *n : all) {
                if (n->parentId == parent->entityId) {
                    n->depthHint = parent->depthHint + 1;
                    next.push_back(n);
                }
            }
        }
        current = std::move(next);
    }

    return levels;
}

inline void HierarchySystem::update() {
    auto all = HierarchyNode::snapshot();
    std::unordered_map<EntityId, HierarchyNode *> idMap;
    auto levels = buildLevels(all, idMap);

    for (const auto &level : levels) {
        for (HierarchyNode *n : level) {
            auto pit = idMap.find(n->parentId);
            if (pit == idMap.end()) {
                // Root: world == local
                n->worldTransform = n->localTransform;
            } else {
                n->worldTransform = pit->second->worldTransform * n->localTransform;
            }
        }
    }
}

template <typename Executor>
inline void HierarchySystem::updateParallel(Executor &&executor) {
    auto all = HierarchyNode::snapshot();
    std::unordered_map<EntityId, HierarchyNode *> idMap;
    auto levels = buildLevels(all, idMap);

    for (const auto &level : levels) {
        // Each node in the same level is independent → submit in parallel.
        std::vector<std::future<void>> tasks;
        tasks.reserve(level.size());
        for (HierarchyNode *n : level) {
            tasks.emplace_back(executor([n, &idMap] {
                auto pit = idMap.find(n->parentId);
                if (pit == idMap.end()) {
                    n->worldTransform = n->localTransform;
                } else {
                    n->worldTransform =
                        pit->second->worldTransform * n->localTransform;
                }
            }));
        }
        // Synchronise the whole level before moving to children.
        for (auto &f : tasks) {
            f.wait();
        }
    }
}

} // namespace ecs::component

#endif // HIERARCHY_H_
