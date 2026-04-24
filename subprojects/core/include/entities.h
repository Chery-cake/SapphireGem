#ifndef ENTITIES_H_
#define ENTITIES_H_

#include <any>
#include <concepts>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unordered_map>

namespace ecs::entity {

template <typename... Components> class Tuple {
public:
  template <typename... Args>
    requires(sizeof...(Args) == sizeof...(Components))
  Tuple(Args &&...args);

  Tuple()
    requires(std::is_default_constructible_v<Components> && ...)
  = default;

  template <typename T>
    requires(std::same_as<T, Components> || ...)
  T &get();

  template <typename T>
    requires(std::same_as<T, Components> || ...)
  const T &get() const;

private:
  std::tuple<Components...> components_;
};

//////////////////////////////////////

template <typename Derived, typename... Components> class Linear;

template <typename Derived, typename First, typename... Rest>
class Linear<Derived, First, Rest...> : public First,
                                        public Linear<Derived, Rest...> {
public:
  Linear()
    requires(std::is_default_constructible_v<First> && ... &&
             std::is_default_constructible_v<Rest>)
  = default;

  template <typename F, typename... R> Linear(F &&first, R &&...rest);

  template <typename T>
    requires(std::same_as<T, First> || (std::same_as<T, Rest> || ...))
  T &get();
};

template <typename Derived> class Linear<Derived> {};

//////////////////////////////////////

template <typename Derived, typename... Components> class Virtual;

template <typename Derived, typename First, typename... Rest>
class Virtual<Derived, First, Rest...>
    : public virtual First, public virtual Virtual<Derived, Rest...> {
public:
  template <typename F, typename... R> Virtual(F &&first, R &&...rest);

  Virtual()
    requires(std::is_default_constructible_v<First> && ... &&
             std::is_default_constructible_v<Rest>)
  = default;

  template <typename T>
    requires(std::same_as<T, First> || (std::same_as<T, Rest> || ...))
  T &get();
};

template <typename Derived> class Virtual<Derived> {};

} // namespace ecs::entity

namespace ecs::component {

struct DynamicStorage {
  std::unordered_map<std::type_index, std::any> components;

  template <typename T> bool has() const {
    return components.contains(typeid(T));
  }

  template <typename T> const T *get() const {
    auto it = components.find(typeid(T));
    return (it != components.end()) ? std::any_cast<T>(&it->second) : nullptr;
  }

  template <typename T> void add(T &&value) {
    components[typeid(T)] = std::forward<T>(value);
  }

  template <typename T> void remove() { components.erase(typeid(T)); }
};

} // namespace ecs::component

// Include implementation details
#include "../src/entities_impl.hpp"

#endif // ENTITIES_H_
