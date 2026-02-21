#include "scene.h"
#include <utility>

namespace window {

Scene::Scene(const SceneTag &tag) : name_(tag.name) {}

Scene::~Scene() = default;

Scene::Scene(Scene &&other) noexcept {
  // Only the source needs locking during construction since the new object
  // cannot yet be accessed by other threads (unlike move-assignment where
  // both objects may already be shared)
  std::lock_guard<std::mutex> lock(other.sceneMutex_);
  name_ = other.name_;
  other.name_ = nullptr;
  loaded_ = other.loaded_;
  other.loaded_ = false;
}

Scene &Scene::operator=(Scene &&other) noexcept {
  if (this != &other) {
    std::scoped_lock lock(sceneMutex_, other.sceneMutex_);
    name_ = other.name_;
    other.name_ = nullptr;
    loaded_ = other.loaded_;
    other.loaded_ = false;
  }
  return *this;
}

} // namespace window
