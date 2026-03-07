#ifndef RESOURCE_REGISTRY_H_
#define RESOURCE_REGISTRY_H_

#include "core_export.h"
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace core {

/**
 * @brief A combined registry for type-safe asset management with metadata-rich
 * tags.
 *
 * This template class combines the benefits of:
 * - Type-safe tag pointers (compile-time safety, no typos)
 * - Tag structs with metadata (name, path, custom fields)
 * - Asset storage with automatic access to tag metadata
 * - Callbacks for add/remove events
 *
 * @warning Tag instances MUST have static storage duration (constexpr, static,
 *          or global). Using local/temporary tag instances will cause undefined
 *          behavior as their addresses may change between calls.
 *
 * Example usage:
 * @code
 *   // Step 1: Define a tag type with metadata
 *   struct TextureTag {
 *     const char *name;
 *     const char *path;
 *     int width;
 *     int height;
 *
 *     constexpr TextureTag(const char* n, const char* p, int w, int h)
 *       : name(n), path(n), width(w), height(h) {}
 *   };
 *
 *   // Step 2: Define constexpr tag instances with their metadata
 *   constexpr TextureTag GRASS_TEXTURE{"grass", "textures/grass.png", 512,
 *   512};
 *   constexpr TextureTag WATER_TEXTURE{"water", "textures/water.png",
 *   1024, 1024};
 *
 *   // Step 3: Define your asset class
 *   class Texture {
 *   public:
 *     // Can use tag metadata during construction!
 *     explicit Texture(const TextureTag& tag)
 *       : _path(tag.path), _width(tag.width), _height(tag.height) {
 *       // Load texture from tag.path with dimensions tag.width x tag.height
 *     }
 *   private:
 *     std::string _path;
 *     int _width, _height;
 *   };
 *
 *   // Step 4: Create registry and use it
 *   core::ResourceRegistry<TextureTag, Texture> textures;
 *
 *   // Add assets - the tag provides all metadata!
 *   textures.add(&GRASS_TEXTURE, std::make_unique<Texture>(GRASS_TEXTURE));
 *
 *   // Or use the convenience method that creates from tag
 *   textures.emplace(&WATER_TEXTURE);  // Constructs Texture(WATER_TEXTURE)
 *
 *   // Get asset and access tag metadata
 *   if (auto* entry = textures.getEntry(&GRASS_TEXTURE)) {
 *     std::cout << "Loaded: " << entry->tag->name << " from " <<
 *     entry->tag->path;
 *   }
 *
 *   // Iterate over all entries
 *   textures.forEach([](const TextureTag* tag, Texture* asset) {
 *     std::cout << tag->name << ": " << tag->path << std::endl;
 *   });
 *
 *   // Register callbacks
 *   textures.onAdd([](const TextureTag* tag, Texture* asset) {
 *     std::cout << "Added texture: " << tag->name << std::endl;
 *   });
 * @endcode
 *
 * @tparam Tag The tag struct type (should inherit from ResourceTag or have
 *             similar interface)
 * @tparam Asset The asset type to store
 */
template <typename Tag, typename Asset> class ResourceRegistry {
private:
  // Callback type
  using AssetCallback = std::function<void(const Tag *, Asset *)>;

  std::unordered_map<const Tag *, std::unique_ptr<Asset>> assets_;
  std::vector<AssetCallback> addCallbacks_;
  std::vector<AssetCallback> removeCallbacks_;
  mutable std::mutex mutex_;

public:
  /**
   * @brief Entry containing both the tag pointer and asset pointer
   */
  struct Entry {
    const Tag *tag;
    Asset *asset;
  };

  ResourceRegistry() = default;
  ~ResourceRegistry() = default;

  // Delete copy and move operations (mutex is not movable)
  ResourceRegistry(const ResourceRegistry &) = delete;
  ResourceRegistry &operator=(const ResourceRegistry &) = delete;
  ResourceRegistry(ResourceRegistry &&) = delete;
  ResourceRegistry &operator=(ResourceRegistry &&) = delete;

  /**
   * @brief Add an asset to the registry under the given tag
   * @param tag Pointer to the tag instance (contains metadata)
   * @param asset Unique pointer to the asset (ownership transferred)
   * @return true if added, false if tag already exists
   */
  bool add(const Tag *tag, std::unique_ptr<Asset> asset) {
    Asset *assetPtr = nullptr;
    bool inserted = false;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto [it, ins] = assets_.try_emplace(tag, std::move(asset));
      inserted = ins;
      if (inserted) {
        assetPtr = it->second.get();
      }
    }

    // Invoke callbacks outside the lock
    if (inserted) {
      for (const auto &callback : addCallbacks_) {
        callback(tag, assetPtr);
      }
    }

    return inserted;
  }

  /**
   * @brief Construct and add an asset using the tag's metadata
   * @param tag Pointer to the tag instance
   * @param args Additional arguments to forward to Asset constructor after
   * tag
   * @return true if added, false if tag already exists
   */
  template <typename... Args> bool emplace(const Tag *tag, Args &&...args) {
    return add(tag, std::make_unique<Asset>(*tag, std::forward<Args>(args)...));
  }

  /**
   * @brief Replace an existing asset or add if not present
   * @param tag Pointer to the tag instance
   * @param asset Unique pointer to the asset
   * @return true if a new asset was added, false if an existing asset was
   * replaced
   *
   * @note Add callbacks are invoked only when a new entry is added.
   *       When replacing, remove callbacks are invoked first, then add
   *       callbacks.
   */
  bool set(const Tag *tag, std::unique_ptr<Asset> asset) {
    Asset *assetPtr = nullptr;
    bool wasNew = false;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = assets_.find(tag);
      wasNew = (it == assets_.end());

      assets_[tag] = std::move(asset);
      assetPtr = assets_[tag].get();
    }

    // Invoke remove callbacks first if replacing
    if (!wasNew) {
      for (const auto &callback : removeCallbacks_) {
        callback(tag, assetPtr);
      }
    }

    // Then invoke add callbacks
    for (const auto &callback : addCallbacks_) {
      callback(tag, assetPtr);
    }

    return wasNew;
  }

  /**
   * @brief Get an asset by tag
   * @param tag Pointer to the tag instance
   * @return Pointer to the asset, or nullptr if not found
   */
  Asset *get(const Tag *tag) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = assets_.find(tag);
    return it != assets_.end() ? it->second.get() : nullptr;
  }

  /**
   * @brief Get both the tag and asset as an Entry
   * @param tag Pointer to the tag instance
   * @return Entry with tag and asset pointers, or {nullptr, nullptr} if not
   * found
   */
  Entry getEntry(const Tag *tag) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = assets_.find(tag);
    if (it != assets_.end()) {
      return Entry{tag, it->second.get()};
    }
    return Entry{nullptr, nullptr};
  }

  /**
   * @brief Check if an asset exists for the given tag
   * @param tag Pointer to the tag instance
   * @return true if asset exists
   */
  bool contains(const Tag *tag) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return assets_.find(tag) != assets_.end();
  }

  /**
   * @brief Remove an asset from the registry
   * @param tag Pointer to the tag instance
   * @return true if removed, false if tag didn't exist
   */
  bool remove(const Tag *tag) {
    Asset *assetPtr = nullptr;
    bool removed = false;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = assets_.find(tag);
      if (it != assets_.end()) {
        assetPtr = it->second.get();
        assets_.erase(it);
        removed = true;
      }
    }

    if (removed) {
      for (const auto &callback : removeCallbacks_) {
        callback(tag, assetPtr);
      }
    }

    return removed;
  }

  /**
   * @brief Remove and return an asset from the registry
   * @param tag Pointer to the tag instance
   * @return Unique pointer to the asset, or nullptr if not found
   */
  std::unique_ptr<Asset> extract(const Tag *tag) {
    Asset *assetPtr = nullptr;
    std::unique_ptr<Asset> result;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = assets_.find(tag);
      if (it != assets_.end()) {
        assetPtr = it->second.get();
        result = std::move(it->second);
        assets_.erase(it);
      }
    }

    if (result) {
      for (const auto &callback : removeCallbacks_) {
        callback(tag, assetPtr);
      }
    }

    return result;
  }

  /**
   * @brief Iterate over all entries (tag + asset pairs)
   * @param func Function to call for each entry: void(const Tag*, Asset*)
   *
   * @warning The mutex is held for the entire duration of iteration.
   *          Do NOT call other registry methods from within the callback,
   *          as this will cause a deadlock. Use getAll() if you need to
   *          modify the registry during iteration.
   */
  template <typename Func> void forEach(Func &&func) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &[tag, asset] : assets_) {
      func(tag, asset.get());
    }
  }

  /**
   * @brief Get all entries as a vector (for iteration outside lock)
   * @return Vector of Entry structs
   */
  std::vector<Entry> getAll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Entry> entries;
    entries.reserve(assets_.size());
    for (const auto &[tag, asset] : assets_) {
      entries.push_back(Entry{tag, asset.get()});
    }
    return entries;
  }

  /**
   * @brief Clear all assets from the registry
   */
  void clear() {
    std::vector<Entry> entries;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (const auto &[tag, asset] : assets_) {
        entries.push_back({tag, asset.get()});
      }
      assets_.clear();
    }

    for (const Entry &entry : entries) {
      for (const auto &callback : removeCallbacks_) {
        callback(entry.tag, entry.asset);
      }
    }
  }

  /**
   * @brief Get the number of assets in the registry
   * @return Number of registered assets
   */
  size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return assets_.size();
  }

  /**
   * @brief Check if the registry is empty
   * @return true if no assets registered
   */
  bool empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return assets_.empty();
  }

  /**
   * @brief Register a callback for when assets are added
   * @param callback Function to call: void(const Tag*, Asset*)
   */
  void onAdd(AssetCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    addCallbacks_.push_back(std::move(callback));
  }

  /**
   * @brief Register a callback for when assets are removed
   * @param callback Function to call: void(const Tag*)
   */
  void onRemove(AssetCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    removeCallbacks_.push_back(std::move(callback));
  }

  /**
   * @brief Clear all callbacks
   */
  void clearCallbacks() {
    std::lock_guard<std::mutex> lock(mutex_);
    addCallbacks_.clear();
    removeCallbacks_.clear();
  }
};
}; // namespace core

#endif // RESOURCE_REGISTRY_H_
