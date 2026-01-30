#ifndef TAG_REGISTRY_H_
#define TAG_REGISTRY_H_

#include "core_export.h"
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace core {

/**
 * @brief A registry for managing tagged resources that can be dynamically
 *        added and removed as modules are loaded/unloaded.
 *
 * This class provides a thread-safe way to manage resources (like shaders,
 * textures, etc.) that are associated with string tags. Resources can be
 * registered when modules are loaded and unregistered when modules are
 * unloaded.
 *
 * Example usage:
 * @code
 *   auto& registry = TagRegistry::instance();
 *   registry.add("shader", "default_vertex");
 *   registry.add("shader", "default_fragment");
 *   registry.add("texture", "diffuse_map");
 *
 *   // Get all items for a tag
 *   auto shaders = registry.get("shader");
 *
 *   // Remove items when unloading
 *   registry.remove("shader", "default_vertex");
 * @endcode
 */
class CORE_API TagRegistry {
public:
  // Singleton access
  static TagRegistry &instance();

  // Delete copy and move operations
  TagRegistry(const TagRegistry &) = delete;
  TagRegistry &operator=(const TagRegistry &) = delete;
  TagRegistry(TagRegistry &&) = delete;
  TagRegistry &operator=(TagRegistry &&) = delete;

  /**
   * @brief Add an item to a tag
   * @param tag The tag to add the item to
   * @param item The item to add
   * @return true if the item was added, false if it already existed
   */
  bool add(const std::string &tag, const std::string &item);

  /**
   * @brief Remove an item from a tag
   * @param tag The tag to remove the item from
   * @param item The item to remove
   * @return true if the item was removed, false if it didn't exist
   */
  bool remove(const std::string &tag, const std::string &item);

  /**
   * @brief Check if an item exists in a tag
   * @param tag The tag to check
   * @param item The item to look for
   * @return true if the item exists in the tag
   */
  bool contains(const std::string &tag, const std::string &item) const;

  /**
   * @brief Get all items for a tag
   * @param tag The tag to get items for
   * @return Vector of items associated with the tag
   */
  std::vector<std::string> get(const std::string &tag) const;

  /**
   * @brief Get all registered tags
   * @return Vector of all tag names
   */
  std::vector<std::string> getTags() const;

  /**
   * @brief Get the number of items in a tag
   * @param tag The tag to count items for
   * @return Number of items in the tag
   */
  size_t count(const std::string &tag) const;

  /**
   * @brief Clear all items from a tag
   * @param tag The tag to clear
   */
  void clear(const std::string &tag);

  /**
   * @brief Clear all tags and items
   */
  void clearAll();

  /**
   * @brief Register a callback to be called when items are added to a tag
   * @param tag The tag to watch
   * @param callback Function to call with the added item
   */
  void onAdd(const std::string &tag,
             std::function<void(const std::string &)> callback);

  /**
   * @brief Register a callback to be called when items are removed from a tag
   * @param tag The tag to watch
   * @param callback Function to call with the removed item
   */
  void onRemove(const std::string &tag,
                std::function<void(const std::string &)> callback);

  /**
   * @brief Clear all callbacks for a specific tag
   * @param tag The tag to clear callbacks for
   */
  void clearCallbacks(const std::string &tag);

  /**
   * @brief Clear all callbacks
   */
  void clearAllCallbacks();

#ifdef ENGINE_DEBUG
  // Hot reload support: set/get the singleton instance
  static void setInstance(TagRegistry *inst);
  static TagRegistry *getInstance();
  // In debug mode, allow direct instantiation for hot reload
  TagRegistry() = default;
  ~TagRegistry() = default;
#else
private:
  TagRegistry() = default;
  ~TagRegistry() = default;
#endif

private:
  // Map from tag name to set of items
  std::unordered_map<std::string, std::unordered_set<std::string>> _registry;

  // Callbacks for add/remove events
  std::unordered_map<std::string,
                     std::vector<std::function<void(const std::string &)>>>
      _addCallbacks;
  std::unordered_map<std::string,
                     std::vector<std::function<void(const std::string &)>>>
      _removeCallbacks;

  // Mutex for thread safety
  mutable std::mutex _mutex;
};

} // namespace core

#endif // TAG_REGISTRY_H_
