#include "tag_registry.h"
#include <algorithm>

namespace core {

#ifdef ENGINE_DEBUG
static TagRegistry *g_tagRegistryInstance = nullptr;
#endif

TagRegistry &TagRegistry::instance() {
#ifdef ENGINE_DEBUG
  if (g_tagRegistryInstance) {
    return *g_tagRegistryInstance;
  }
#endif
  static TagRegistry instance;
#ifdef ENGINE_DEBUG
  g_tagRegistryInstance = &instance;
#endif
  return instance;
}

#ifdef ENGINE_DEBUG
void TagRegistry::setInstance(TagRegistry *inst) {
  g_tagRegistryInstance = inst;
}

TagRegistry *TagRegistry::getInstance() { return g_tagRegistryInstance; }
#endif

bool TagRegistry::add(const std::string &tag, const std::string &item) {
  std::vector<std::function<void(const std::string &)>> callbacksCopy;
  bool inserted = false;

  {
    std::lock_guard<std::mutex> lock(_mutex);
    auto [it, ins] = _registry[tag].insert(item);
    inserted = ins;

    // Copy callbacks while holding the lock
    if (inserted) {
      auto callbackIt = _addCallbacks.find(tag);
      if (callbackIt != _addCallbacks.end()) {
        callbacksCopy = callbackIt->second;
      }
    }
  }

  // Invoke callbacks outside the lock to prevent deadlock
  for (const auto &callback : callbacksCopy) {
    callback(item);
  }

  return inserted;
}

bool TagRegistry::remove(const std::string &tag, const std::string &item) {
  std::vector<std::function<void(const std::string &)>> callbacksCopy;
  bool removed = false;

  {
    std::lock_guard<std::mutex> lock(_mutex);

    auto tagIt = _registry.find(tag);
    if (tagIt == _registry.end()) {
      return false;
    }

    size_t removedCount = tagIt->second.erase(item);
    removed = removedCount > 0;

    // Copy callbacks while holding the lock
    if (removed) {
      auto callbackIt = _removeCallbacks.find(tag);
      if (callbackIt != _removeCallbacks.end()) {
        callbacksCopy = callbackIt->second;
      }

      // Clean up empty tags
      if (tagIt->second.empty()) {
        _registry.erase(tagIt);
      }
    }
  }

  // Invoke callbacks outside the lock to prevent deadlock
  for (const auto &callback : callbacksCopy) {
    callback(item);
  }

  return removed;
}

bool TagRegistry::contains(const std::string &tag,
                           const std::string &item) const {
  std::lock_guard<std::mutex> lock(_mutex);

  auto tagIt = _registry.find(tag);
  if (tagIt == _registry.end()) {
    return false;
  }

  return tagIt->second.find(item) != tagIt->second.end();
}

std::vector<std::string> TagRegistry::get(const std::string &tag) const {
  std::lock_guard<std::mutex> lock(_mutex);

  auto tagIt = _registry.find(tag);
  if (tagIt == _registry.end()) {
    return {};
  }

  return std::vector<std::string>(tagIt->second.begin(), tagIt->second.end());
}

std::vector<std::string> TagRegistry::getTags() const {
  std::lock_guard<std::mutex> lock(_mutex);

  std::vector<std::string> tags;
  tags.reserve(_registry.size());

  for (const auto &[tag, items] : _registry) {
    tags.push_back(tag);
  }

  return tags;
}

size_t TagRegistry::count(const std::string &tag) const {
  std::lock_guard<std::mutex> lock(_mutex);

  auto tagIt = _registry.find(tag);
  if (tagIt == _registry.end()) {
    return 0;
  }

  return tagIt->second.size();
}

void TagRegistry::clear(const std::string &tag) {
  std::vector<std::string> itemsCopy;
  std::vector<std::function<void(const std::string &)>> callbacksCopy;

  {
    std::lock_guard<std::mutex> lock(_mutex);

    auto tagIt = _registry.find(tag);
    if (tagIt != _registry.end()) {
      // Copy items and callbacks while holding the lock
      itemsCopy =
          std::vector<std::string>(tagIt->second.begin(), tagIt->second.end());

      auto callbackIt = _removeCallbacks.find(tag);
      if (callbackIt != _removeCallbacks.end()) {
        callbacksCopy = callbackIt->second;
      }

      _registry.erase(tagIt);
    }
  }

  // Invoke callbacks outside the lock to prevent deadlock
  for (const auto &item : itemsCopy) {
    for (const auto &callback : callbacksCopy) {
      callback(item);
    }
  }
}

void TagRegistry::clearAll() {
  // Copy all data needed for callbacks while holding the lock
  std::vector<std::pair<std::string, std::vector<std::string>>> tagItemsCopy;
  std::unordered_map<std::string,
                     std::vector<std::function<void(const std::string &)>>>
      callbacksCopy;

  {
    std::lock_guard<std::mutex> lock(_mutex);

    for (const auto &[tag, items] : _registry) {
      tagItemsCopy.emplace_back(
          tag, std::vector<std::string>(items.begin(), items.end()));
    }
    callbacksCopy = _removeCallbacks;

    _registry.clear();
  }

  // Invoke callbacks outside the lock to prevent deadlock
  for (const auto &[tag, items] : tagItemsCopy) {
    auto callbackIt = callbacksCopy.find(tag);
    if (callbackIt != callbacksCopy.end()) {
      for (const auto &item : items) {
        for (const auto &callback : callbackIt->second) {
          callback(item);
        }
      }
    }
  }
}

void TagRegistry::onAdd(const std::string &tag,
                        std::function<void(const std::string &)> callback) {
  std::lock_guard<std::mutex> lock(_mutex);
  _addCallbacks[tag].push_back(std::move(callback));
}

void TagRegistry::onRemove(const std::string &tag,
                           std::function<void(const std::string &)> callback) {
  std::lock_guard<std::mutex> lock(_mutex);
  _removeCallbacks[tag].push_back(std::move(callback));
}

void TagRegistry::clearCallbacks(const std::string &tag) {
  std::lock_guard<std::mutex> lock(_mutex);
  _addCallbacks.erase(tag);
  _removeCallbacks.erase(tag);
}

void TagRegistry::clearAllCallbacks() {
  std::lock_guard<std::mutex> lock(_mutex);
  _addCallbacks.clear();
  _removeCallbacks.clear();
}

} // namespace core
