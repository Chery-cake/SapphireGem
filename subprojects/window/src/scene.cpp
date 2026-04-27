#include "scene.h"
#include "object.h"
#include <algorithm>
#include <mutex>
#include <ranges>

namespace window {

Scene::Scene(const SceneTag &tag) : name_(tag.name) {}

Scene::~Scene() = default;

Scene::Scene(Scene &&other) noexcept {
  std::scoped_lock lock(other.sceneMutex_, other.objectsMutex_);
  name_ = other.name_;
  loaded_ = other.loaded_;
  objects_ = std::move(other.objects_);
  other.loaded_ = false;
}

Scene &Scene::operator=(Scene &&other) noexcept {
  if (this != &other) {
    std::scoped_lock lock(sceneMutex_, other.sceneMutex_, objectsMutex_,
                          other.objectsMutex_);
    name_ = other.name_;
    loaded_ = other.loaded_;
    other.loaded_ = false;
    objects_ = std::move(other.objects_);
  }
  return *this;
}

bool Scene::load(device::GPUDevice & /*device*/,
                 std::vector<device::GPUDevice *> & /*secondaryGPUs*/,
                 device::VMAAllocator & /*allocator*/,
                 device::ShaderManager & /*shaderManager*/,
                 Renderer & /*renderer*/) {
  // Base implementation does nothing – objects must already be added.
  loaded_ = true;
  return true;
}

void Scene::unload() {
  loaded_ = false;
  // Objects own their GPU resources; they will clean up when destroyed.
  // You can optionally release them here.
  // TODO implement
}

void Scene::update(float /*deltaTime*/) {
  // Base empty update
}

void Scene::preRender(vk::CommandBuffer cmd, uint32_t frameIndex) {
  std::lock_guard lock(objectsMutex_);

  std::ranges::for_each(
      objects_ | std::views::filter([](Drawable &d) {
        return d.object && d.object->isInitialized();
      }),
      [cmd, frameIndex](Drawable &d) { d.object->preRender(cmd, frameIndex); });
}

void Scene::draw(vk::CommandBuffer cmd, uint32_t frameIndex) {
  std::lock_guard lock(objectsMutex_);

  std::ranges::for_each(objects_ | std::views::filter([](Drawable &d) {
                          return d.object && d.object->isInitialized();
                        }),
                        [cmd, frameIndex](Drawable &d) {
                          if (d.update)
                            d.update(frameIndex);
                          d.object->draw(cmd, frameIndex);
                        });
}

void Scene::addObject(std::shared_ptr<ObjectBase> obj,
                      ObjectUpdateFunc updateFunc) {
  std::unique_lock lock(objectsMutex_);
  objects_.push_back(Drawable(std::move(obj), std::move(updateFunc)));
  auto *ptr = objects_.back().object.get();

  lock.unlock();
  objectAdded.emit(ptr);
}

void Scene::removeObject(const ObjectBase *obj) {
  std::unique_lock lock(objectsMutex_);
  auto it = std::ranges::find_if(
      objects_, [obj](const Drawable &d) { return d.object.get() == obj; });
  if (it == objects_.end()) {
    return;
  }

  auto *ptr = it->object.get();
  objects_.erase(it);

  lock.unlock();
  objectRemoved.emit(ptr);
}

void Scene::clearObjects() {
  std::lock_guard lock(objectsMutex_);
  objects_.clear();
}

} // namespace window
