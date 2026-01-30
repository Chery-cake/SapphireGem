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
  std::lock_guard<std::mutex> lock(_mutex);

  auto [it, inserted] = _registry[tag].insert(item);

  // Call add callbacks if item was actually added
  if (inserted) {
    auto callbackIt = _addCallbacks.find(tag);
    if (callbackIt != _addCallbacks.end()) {
      for (const auto &callback : callbackIt->second) {
        callback(item);
      }
    }
  }

  return inserted;
}

bool TagRegistry::remove(const std::string &tag, const std::string &item) {
  std::lock_guard<std::mutex> lock(_mutex);

  auto tagIt = _registry.find(tag);
  if (tagIt == _registry.end()) {
    return false;
  }

  size_t removed = tagIt->second.erase(item);

  // Call remove callbacks if item was actually removed
  if (removed > 0) {
    auto callbackIt = _removeCallbacks.find(tag);
    if (callbackIt != _removeCallbacks.end()) {
      for (const auto &callback : callbackIt->second) {
        callback(item);
      }
    }

    // Clean up empty tags
    if (tagIt->second.empty()) {
      _registry.erase(tagIt);
    }
  }

  return removed > 0;
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
  std::lock_guard<std::mutex> lock(_mutex);

  auto tagIt = _registry.find(tag);
  if (tagIt != _registry.end()) {
    // Call remove callbacks for each item
    auto callbackIt = _removeCallbacks.find(tag);
    if (callbackIt != _removeCallbacks.end()) {
      for (const auto &item : tagIt->second) {
        for (const auto &callback : callbackIt->second) {
          callback(item);
        }
      }
    }

    _registry.erase(tagIt);
  }
}

void TagRegistry::clearAll() {
  std::lock_guard<std::mutex> lock(_mutex);

  // Call remove callbacks for all items
  for (const auto &[tag, items] : _registry) {
    auto callbackIt = _removeCallbacks.find(tag);
    if (callbackIt != _removeCallbacks.end()) {
      for (const auto &item : items) {
        for (const auto &callback : callbackIt->second) {
          callback(item);
        }
      }
    }
  }

  _registry.clear();
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
