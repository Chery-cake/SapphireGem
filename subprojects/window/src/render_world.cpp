#include "render_world.h"
#include <algorithm>
#include <mutex>
#include <ranges>

namespace window {

// ── Registration ──────────────────────────────────────────────────────────

void RenderWorld::add(ecs::component::object::RenderComponentBase *rc,
                      RenderUpdateFunc updateFunc) {
  if (rc == nullptr) {
    return;
  }

  {
    std::lock_guard lock(mutex_);
    // No duplicates
    const bool exists =
        std::ranges::any_of(entries_, [rc](const Entry &e) {
          return e.renderComp == rc;
        });
    if (exists) {
      return;
    }
    entries_.emplace_back(rc, std::move(updateFunc));
  }

  objectAdded.emit(rc);
}

void RenderWorld::remove(
    const ecs::component::object::RenderComponentBase *rc) {
  if (rc == nullptr) {
    return;
  }

  ecs::component::object::RenderComponentBase *ptr = nullptr;
  {
    std::lock_guard lock(mutex_);
    auto it = std::ranges::find_if(entries_, [rc](const Entry &e) {
      return e.renderComp == rc;
    });
    if (it == entries_.end()) {
      return;
    }
    ptr = it->renderComp;
    entries_.erase(it);
  }

  objectRemoved.emit(ptr);
}

void RenderWorld::clear() {
  std::lock_guard lock(mutex_);
  entries_.clear();
}

// ── Rendering ─────────────────────────────────────────────────────────────

void RenderWorld::draw(vk::CommandBuffer cmd, uint32_t frameIndex) const {
  std::lock_guard lock(mutex_);
  std::ranges::for_each(
      entries_ |
          std::views::filter([](const Entry &e) { return e.renderComp; }),
      [cmd, frameIndex](const Entry &e) {
        if (e.update) {
          e.update(frameIndex);
        }
        e.renderComp->draw(cmd, frameIndex);
      });
}

} // namespace window
