#ifndef COMPONENT_REGISTRY_H_
#define COMPONENT_REGISTRY_H_

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace ecs::component {

/**
 * @brief CRTP mixin that maintains a global thread-safe list of all live
 *        instances of a component type.
 *
 * Inherit from ComponentRegistry<T> (with T = the concrete class) to have
 * every live instance automatically register itself in a process-wide static
 * list on construction and unregister on destruction.
 *
 * The list is protected by a per-type @c std::shared_mutex:
 * - @ref forEach / @ref forEachConst / @ref count / @ref snapshot acquire a
 *   *shared* lock and may execute concurrently.
 * - Construction and destruction acquire an *exclusive* lock.
 *
 * @note  Do **not** construct or destroy instances of @c T inside a
 *        @ref forEach or @ref forEachConst callback — doing so acquires the
 *        same mutex exclusively and will deadlock.  Use @ref snapshot first to
 *        obtain a copy of the pointer list, then iterate outside the lock.
 *
 * ### Copy / move semantics
 * - **Copy construction**: the new object is a distinct instance and is
 *   registered independently.
 * - **Copy assignment**: the pointer identity of @c *this does not change,
 *   so no registration update is needed.
 * - **Move construction**: the new object registers itself; the moved-from
 *   object will unregister itself when it is destroyed (as usual).
 * - **Move assignment**: same reasoning as copy assignment — no-op.
 *
 * @tparam Derived  The concrete component class (CRTP parameter).
 *
 * ### Example
 * @code
 *   struct Hitbox : public ecs::component::ComponentRegistry<Hitbox> {
 *       glm::vec3 min, max;
 *   };
 *
 *   // Iterate all live Hitbox instances from any thread:
 *   Hitbox::forEach([](Hitbox& hb) {
 *       processHitbox(hb);
 *   });
 *
 *   // Safe removal during iteration:
 *   auto ptrs = Hitbox::snapshot();
 *   for (Hitbox* p : ptrs) { process(*p); }
 * @endcode
 */
template <typename Derived>
class ComponentRegistry {
public:
    // ── Lifecycle ──────────────────────────────────────────────────────────

    ComponentRegistry() noexcept;
    ~ComponentRegistry() noexcept;

    /// Copy construction: the new object is a separate instance; register it.
    ComponentRegistry(const ComponentRegistry &) noexcept;
    /// Copy assignment: @c this pointer does not change; nothing to update.
    ComponentRegistry &operator=(const ComponentRegistry &) noexcept { return *this; }

    /// Move construction: this is a new object; register it (the moved-from
    /// object will unregister itself on destruction as usual).
    ComponentRegistry(ComponentRegistry &&) noexcept;
    /// Move assignment: @c this pointer does not change; nothing to update.
    ComponentRegistry &operator=(ComponentRegistry &&) noexcept { return *this; }

    // ── Static global query API ────────────────────────────────────────────

    /**
     * @brief Invoke @p fn for every currently live instance of @c Derived.
     *
     * Holds a shared lock for the entire iteration.  The callback must not
     * construct or destroy @c Derived instances (that would deadlock).
     *
     * @param fn  Callable with signature @c void(Derived&).
     */
    static void forEach(std::invocable<Derived &> auto &&fn);

    /**
     * @brief Const variant of @ref forEach.
     *
     * @param fn  Callable with signature @c void(const Derived&).
     */
    static void forEachConst(std::invocable<const Derived &> auto &&fn);

    /**
     * @brief Return the number of currently live instances.
     */
    [[nodiscard]] static std::size_t count();

    /**
     * @brief Return a snapshot copy of all live instance pointers.
     *
     * The snapshot is taken under a shared lock.  The returned pointers are
     * valid only as long as the owning objects remain alive; callers are
     * responsible for lifetime management.
     *
     * This is the safe way to iterate when the callback might add or remove
     * @c Derived objects:
     * @code
     *   auto ptrs = Derived::snapshot();
     *   for (Derived* p : ptrs) { maybeDestroy(*p); }
     * @endcode
     *
     * @return Copy of the internal pointer vector at the moment of the call.
     */
    [[nodiscard]] static std::vector<Derived *> snapshot();

private:
    // ── Per-type singletons (function-local statics) ───────────────────────
    // Using function-local statics avoids the static-initialization-order
    // fiasco and guarantees that both objects outlive any instance of Derived.

    [[nodiscard]] static std::vector<Derived *> &registry_() noexcept;
    [[nodiscard]] static std::shared_mutex &     mutex_()    noexcept;

    void register_()   noexcept;
    void unregister_() noexcept;
};

// ── Implementation ─────────────────────────────────────────────────────────

template <typename Derived>
std::vector<Derived *> &ComponentRegistry<Derived>::registry_() noexcept {
    static std::vector<Derived *> vec;
    return vec;
}

template <typename Derived>
std::shared_mutex &ComponentRegistry<Derived>::mutex_() noexcept {
    static std::shared_mutex mtx;
    return mtx;
}

template <typename Derived>
void ComponentRegistry<Derived>::register_() noexcept {
    std::unique_lock lock(mutex_());
    registry_().push_back(static_cast<Derived *>(this));
}

template <typename Derived>
void ComponentRegistry<Derived>::unregister_() noexcept {
    std::unique_lock lock(mutex_());
    auto &vec = registry_();
    auto  it  = std::ranges::find(vec, static_cast<Derived *>(this));
    if (it != vec.end()) {
        // Swap with last element for O(1) removal; order is unspecified anyway.
        *it = vec.back();
        vec.pop_back();
    }
}

template <typename Derived>
ComponentRegistry<Derived>::ComponentRegistry() noexcept {
    register_();
}

template <typename Derived>
ComponentRegistry<Derived>::~ComponentRegistry() noexcept {
    unregister_();
}

template <typename Derived>
ComponentRegistry<Derived>::ComponentRegistry(const ComponentRegistry &) noexcept {
    register_();
}

template <typename Derived>
ComponentRegistry<Derived>::ComponentRegistry(ComponentRegistry &&) noexcept {
    register_();
}

template <typename Derived>
void ComponentRegistry<Derived>::forEach(std::invocable<Derived &> auto &&fn) {
    std::shared_lock lock(mutex_());
    for (Derived *p : registry_()) {
        fn(*p);
    }
}

template <typename Derived>
void ComponentRegistry<Derived>::forEachConst(
    std::invocable<const Derived &> auto &&fn) {
    std::shared_lock lock(mutex_());
    for (const Derived *p : registry_()) {
        fn(*p);
    }
}

template <typename Derived>
std::size_t ComponentRegistry<Derived>::count() {
    std::shared_lock lock(mutex_());
    return registry_().size();
}

template <typename Derived>
std::vector<Derived *> ComponentRegistry<Derived>::snapshot() {
    std::shared_lock lock(mutex_());
    return registry_(); // copy-under-lock
}

} // namespace ecs::component

#endif // COMPONENT_REGISTRY_H_
