#include "scene.h"
#include "object.h"
#include <mutex>

namespace window {

Scene::Scene(const SceneTag &tag)
    : renderWorld_(std::make_unique<RenderWorld>()), name_(tag.name) {
  // Forward RenderWorld signals to the Scene's own signals so that any
  // observer wired to objectAdded/objectRemoved on the Scene continues
  // to receive notifications.
  renderWorld_->objectAdded.connect([this](
      const ecs::component::object::RenderComponentBase *rc) {
    objectAdded.emit(rc);
  });
  renderWorld_->objectRemoved.connect([this](
      const ecs::component::object::RenderComponentBase *rc) {
    objectRemoved.emit(rc);
  });
}

Scene::~Scene() = default;

Scene::Scene(Scene &&other) noexcept {
  std::lock_guard lock(other.sceneMutex_);
  name_          = other.name_;
  loaded_        = other.loaded_;
  renderWorld_   = std::move(other.renderWorld_);
  other.loaded_  = false;
  // Re-wire signals: the existing lambdas captured `&other`; reconnect them
  // to forward to this new Scene's signals instead.
  if (renderWorld_) {
    renderWorld_->objectAdded.clear();
    renderWorld_->objectRemoved.clear();
    renderWorld_->objectAdded.connect([this](
        const ecs::component::object::RenderComponentBase *rc) {
      objectAdded.emit(rc);
    });
    renderWorld_->objectRemoved.connect([this](
        const ecs::component::object::RenderComponentBase *rc) {
      objectRemoved.emit(rc);
    });
  }
}

Scene &Scene::operator=(Scene &&other) noexcept {
  if (this != &other) {
    std::scoped_lock lock(sceneMutex_, other.sceneMutex_);
    name_         = other.name_;
    loaded_       = other.loaded_;
    other.loaded_ = false;
    renderWorld_  = std::move(other.renderWorld_);
    // Re-wire signals after move
    if (renderWorld_) {
      renderWorld_->objectAdded.clear();
      renderWorld_->objectRemoved.clear();
      renderWorld_->objectAdded.connect([this](
          const ecs::component::object::RenderComponentBase *rc) {
        objectAdded.emit(rc);
      });
      renderWorld_->objectRemoved.connect([this](
          const ecs::component::object::RenderComponentBase *rc) {
        objectRemoved.emit(rc);
      });
    }
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

void Scene::onComputeAttach(AsyncComputeManager * /*manager*/,
                            FrameUpdateSignal * /*signal*/) {
  // Base no-op – override in derived scenes that use compute
}

void Scene::onComputeDetach(AsyncComputeManager * /*manager*/) {
  // Base no-op – override in derived scenes that use compute
}

void Scene::draw(vk::CommandBuffer cmd, uint32_t frameIndex) {
  if (renderWorld_) {
    renderWorld_->draw(cmd, frameIndex);
  }
}

void Scene::addEntity(ecs::component::object::RenderComponentBase *rc,
                      ObjectUpdateFunc updateFunc) {
  if (renderWorld_) {
    renderWorld_->add(rc, std::move(updateFunc));
  }
}

void Scene::removeEntity(
    const ecs::component::object::RenderComponentBase *rc) {
  if (renderWorld_) {
    renderWorld_->remove(rc);
  }
}

void Scene::clearEntities() {
  if (renderWorld_) {
    renderWorld_->clear();
  }
}

} // namespace window

