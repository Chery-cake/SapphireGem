#ifndef ASSET_REGISTRY_H_
#define ASSET_REGISTRY_H_

#include "core_export.h"
#include <memory>
#include <mutex>
#include <unordered_map>

namespace core {

/**
 * @brief A type-safe registry for managing assets using compile-time tag types.
 *
 * This template class provides a way to register and retrieve assets using
 * pointer-to-tag as keys, ensuring type safety at compile time and preventing
 * typos that could occur with string-based keys.
 *
 * To use this registry:
 * 1. Define an empty struct as a tag type for your asset category
 * 2. Create constexpr instances of that tag type for each specific asset
 * 3. Create an AssetRegistry instantiation for your tag/asset combination
 *
 * @warning Tag instances MUST have static storage duration (constexpr, static,
 *          or global). Using local/temporary tag instances will cause undefined
 *          behavior as their addresses may change between calls.
 *
 * Example usage:
 * @code
 *   // Step 1: Define tag types for different asset categories
 *   struct TextureTag {};
 *   struct ShaderTag {};
 *
 *   // Step 2: Define constexpr tag instances (MUST be constexpr/static!)
 *   // These have static storage duration, so their addresses are stable.
 *   constexpr TextureTag GRASS_TEXTURE{};
 *   constexpr TextureTag WATER_TEXTURE{};
 *   constexpr TextureTag SKY_TEXTURE{};
 *
 *   constexpr ShaderTag PBR_SHADER{};
 *   constexpr ShaderTag UNLIT_SHADER{};
 *
 *   // Step 3: Define your asset classes
 *   class Texture {
 *   public:
 *     explicit Texture(const std::string& path) : _path(path) {}
 *     const std::string& path() const { return _path; }
 *   private:
 *     std::string _path;
 *   };
 *
 *   // Step 4: Create registry and use it
 *   AssetRegistry<TextureTag, Texture> textureRegistry;
 *
 *   // Register assets - using the wrong tag type is a compile error!
 *   textureRegistry.add(&GRASS_TEXTURE, std::make_unique<Texture>("grass.png"));
 *   textureRegistry.add(&WATER_TEXTURE, std::make_unique<Texture>("water.png"));
 *
 *   // Retrieve assets
 *   if (Texture* tex = textureRegistry.get(&GRASS_TEXTURE)) {
 *     // Use texture
 *   }
 *
 *   // This would be a compile error:
 *   // textureRegistry.add(&PBR_SHADER, ...);  // Wrong tag type!
 *
 *   // Remove assets when module unloads
 *   textureRegistry.remove(&GRASS_TEXTURE);
 * @endcode
 *
 * @tparam Tag The tag struct type used for keys (e.g., TextureTag, ShaderTag)
 * @tparam Asset The asset type to store (e.g., Texture, Shader)
 *
 * @note The registry stores unique_ptr to assets, taking ownership.
 * @note All operations are thread-safe.
 */
template <typename Tag, typename Asset>
class AssetRegistry {
public:
  AssetRegistry() = default;
  ~AssetRegistry() = default;

  // Delete copy and move operations (mutex is not movable)
  AssetRegistry(const AssetRegistry &) = delete;
  AssetRegistry &operator=(const AssetRegistry &) = delete;
  AssetRegistry(AssetRegistry &&) = delete;
  AssetRegistry &operator=(AssetRegistry &&) = delete;

  /**
   * @brief Add an asset to the registry under the given tag
   * @param tag Pointer to the tag instance (address is used as key)
   * @param asset Unique pointer to the asset (ownership transferred)
   * @return true if added, false if tag already exists (asset not added)
   */
  bool add(const Tag *tag, std::unique_ptr<Asset> asset) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto [it, inserted] = _assets.try_emplace(tag, std::move(asset));
    return inserted;
  }

  /**
   * @brief Replace an existing asset or add if not present
   * @param tag Pointer to the tag instance
   * @param asset Unique pointer to the asset (ownership transferred)
   */
  void set(const Tag *tag, std::unique_ptr<Asset> asset) {
    std::lock_guard<std::mutex> lock(_mutex);
    _assets[tag] = std::move(asset);
  }

  /**
   * @brief Get an asset by tag
   * @param tag Pointer to the tag instance
   * @return Pointer to the asset, or nullptr if not found
   */
  Asset *get(const Tag *tag) const {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _assets.find(tag);
    return it != _assets.end() ? it->second.get() : nullptr;
  }

  /**
   * @brief Check if an asset exists for the given tag
   * @param tag Pointer to the tag instance
   * @return true if asset exists
   */
  bool contains(const Tag *tag) const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _assets.find(tag) != _assets.end();
  }

  /**
   * @brief Remove an asset from the registry
   * @param tag Pointer to the tag instance
   * @return true if removed, false if tag didn't exist
   */
  bool remove(const Tag *tag) {
    std::lock_guard<std::mutex> lock(_mutex);
    return _assets.erase(tag) > 0;
  }

  /**
   * @brief Remove and return an asset from the registry
   * @param tag Pointer to the tag instance
   * @return Unique pointer to the asset, or nullptr if not found
   */
  std::unique_ptr<Asset> extract(const Tag *tag) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _assets.find(tag);
    if (it == _assets.end()) {
      return nullptr;
    }
    auto asset = std::move(it->second);
    _assets.erase(it);
    return asset;
  }

  /**
   * @brief Clear all assets from the registry
   */
  void clear() {
    std::lock_guard<std::mutex> lock(_mutex);
    _assets.clear();
  }

  /**
   * @brief Get the number of assets in the registry
   * @return Number of registered assets
   */
  size_t size() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _assets.size();
  }

  /**
   * @brief Check if the registry is empty
   * @return true if no assets registered
   */
  bool empty() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _assets.empty();
  }

private:
  std::unordered_map<const Tag *, std::unique_ptr<Asset>> _assets;
  mutable std::mutex _mutex;
};

} // namespace core

#endif // ASSET_REGISTRY_H_
