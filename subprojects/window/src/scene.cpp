#include "scene.h"
#include "object.h"
#include <algorithm>
#include <mutex>
#include <ranges>

namespace window {

Scene::Scene(const SceneTag &tag) : name_(tag.name) {}

Scene::~Scene() = default;

Scene::Scene(Scene &&other) noexcept {
  std::scoped_lock lock(other.sceneMutex_, other.entitiesMutex_);
  name_ = other.name_;
  loaded_ = other.loaded_;
  entities_ = std::move(other.entities_);
  other.loaded_ = false;
}

Scene &Scene::operator=(Scene &&other) noexcept {
  if (this != &other) {
    std::scoped_lock lock(sceneMutex_, other.sceneMutex_, entitiesMutex_,
                          other.entitiesMutex_);
    name_ = other.name_;
    loaded_ = other.loaded_;
    other.loaded_ = false;
    entities_ = std::move(other.entities_);
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
  std::lock_guard lock(entitiesMutex_);

  std::ranges::for_each(
      entities_ | std::views::filter([](Drawable &d) { return d.renderComp; }),
      [cmd, frameIndex](Drawable &d) {
        d.renderComp->preRender(cmd, frameIndex);
      });
}

void Scene::draw(vk::CommandBuffer cmd, uint32_t frameIndex) {
  std::lock_guard lock(entitiesMutex_);

  std::ranges::for_each(
      entities_ | std::views::filter([](Drawable &d) { return d.renderComp; }),
      [cmd, frameIndex](Drawable &d) {
        if (d.update) {
          d.update(frameIndex);
        }
        d.renderComp->draw(cmd, frameIndex);
      });
}

void Scene::addEntity(ecs::component::object::RenderComponentBase *rc,
                      ObjectUpdateFunc updateFunc) {
  std::unique_lock lock(entitiesMutex_);
  entities_.push_back(Drawable(rc, std::move(updateFunc)));
  auto *ptr = entities_.back().renderComp;

  lock.unlock();
  objectAdded.emit(ptr);
}

void Scene::removeEntity(
    const ecs::component::object::RenderComponentBase *rc) {
  std::unique_lock lock(entitiesMutex_);
  auto it = std::ranges::find_if(
      entities_, [&rc](const Drawable &d) { return d.renderComp == rc; });
  if (it == entities_.end()) {
    return;
  }

  auto *ptr = it->renderComp;
  entities_.erase(it);

  lock.unlock();
  objectRemoved.emit(ptr);
}

void Scene::clearEntities() {
  std::lock_guard lock(entitiesMutex_);
  entities_.clear();
}

} // namespace window
