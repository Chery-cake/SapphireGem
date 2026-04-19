#include "resource_registry.h"
#include <algorithm>
#include <execution>
#include <iterator>
#include <memory>
#include <mutex>

namespace core {

template <typename Tag, typename Asset>
bool ResourceRegistry<Tag, Asset>::add(const Tag *tag,
                                       std::unique_ptr<Asset> asset) {
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
    std::ranges::for_each(
        addCallbacks_, [&](const auto &callback) { callback(tag, assetPtr); });
  }

  return inserted;
}

template <typename Tag, typename Asset>
template <typename... Args>
bool ResourceRegistry<Tag, Asset>::emplace(const Tag *tag, Args &&...args) {
  return add(tag, std::make_unique<Asset>(*tag, std::forward<Args>(args)...));
}

template <typename Tag, typename Asset>
bool ResourceRegistry<Tag, Asset>::set(const Tag *tag,
                                       std::unique_ptr<Asset> asset) {
  Asset *assetPtr = nullptr;
  bool wasNew = false;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = assets_.find(tag);
    wasNew = (it == assets_.end());
  }

  // Invoke remove callbacks first if replacing
  if (!wasNew) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      assetPtr = assets_[tag].get();
    }
    std::ranges::for_each(removeCallbacks_, [&](const auto &callback) {
      callback(tag, assetPtr);
    });
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    assets_[tag] = std::move(asset);
    assetPtr = assets_[tag].get();
  }

  // Then invoke add callbacks
  std::ranges::for_each(addCallbacks_,
                        [&](const auto &callback) { callback(tag, assetPtr); });

  return wasNew;
}

template <typename Tag, typename Asset>
Asset *ResourceRegistry<Tag, Asset>::get(const Tag *tag) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = assets_.find(tag);
  return it != assets_.end() ? it->second.get() : nullptr;
}

template <typename Tag, typename Asset>
ResourceRegistry<Tag, Asset>::Entry
ResourceRegistry<Tag, Asset>::getEntry(const Tag *tag) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = assets_.find(tag);
  if (it != assets_.end()) {
    return Entry{tag, it->second.get()};
  }
  return Entry{nullptr, nullptr};
}

template <typename Tag, typename Asset>
bool ResourceRegistry<Tag, Asset>::contains(const Tag *tag) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return assets_.find(tag) != assets_.end();
}

template <typename Tag, typename Asset>
bool ResourceRegistry<Tag, Asset>::remove(const Tag *tag) {
  std::unique_ptr<Asset> assetPtr = nullptr;
  bool removed = false;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = assets_.find(tag);
    if (it != assets_.end()) {
      assetPtr = std::move(it->second);
      assets_.erase(it);
      removed = true;
    }
  }

  if (removed) {
    std::ranges::for_each(removeCallbacks_, [&](const auto &callback) {
      callback(tag, assetPtr.get());
    });
  }

  return removed;
}

template <typename Tag, typename Asset>
std::unique_ptr<Asset> ResourceRegistry<Tag, Asset>::extract(const Tag *tag) {
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
    std::ranges::for_each(removeCallbacks_, [&](const auto &callback) {
      callback(tag, assetPtr);
    });
  }

  return result;
}

template <typename Tag, typename Asset>
template <typename Func>
void ResourceRegistry<Tag, Asset>::forEach(Func &&func) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::for_each(std::execution::unseq, assets_.begin(), assets_.end(),
                [&](const auto &pair) { func(pair.first, pair.second.get()); });
}

template <typename Tag, typename Asset>
std::vector<typename ResourceRegistry<Tag, Asset>::Entry>
ResourceRegistry<Tag, Asset>::getAll() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Entry> entries;
  entries.reserve(assets_.size());
  std::ranges::transform(
      assets_, std::back_inserter(entries),
      [](const auto &pair) { return Entry{pair.first, pair.second.get()}; });
  return entries;
}

template <typename Tag, typename Asset>
void ResourceRegistry<Tag, Asset>::clear() {
  std::vector<Entry> entries;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ranges::transform(
        assets_, std::back_inserter(entries),
        [](const auto &pair) { return Entry{pair.first, pair.second.get()}; });
    assets_.clear();
  }

  std::for_each(std::execution::unseq, entries.begin(), entries.end(),
                [&](const Entry &entry) {
                  std::ranges::for_each(removeCallbacks_.begin(),
                                        removeCallbacks_.end(),
                                        [entry](const auto &callback) {
                                          callback(entry.tag, entry.asset);
                                        });
                });
}

template <typename Tag, typename Asset>
size_t ResourceRegistry<Tag, Asset>::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return assets_.size();
}

template <typename Tag, typename Asset>
bool ResourceRegistry<Tag, Asset>::empty() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return assets_.empty();
}

template <typename Tag, typename Asset>
void ResourceRegistry<Tag, Asset>::onAdd(AssetCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  addCallbacks_.push_back(std::move(callback));
}

template <typename Tag, typename Asset>
void ResourceRegistry<Tag, Asset>::onRemove(AssetCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  removeCallbacks_.push_back(std::move(callback));
}

template <typename Tag, typename Asset>
void ResourceRegistry<Tag, Asset>::clearCallbacks() {
  std::lock_guard<std::mutex> lock(mutex_);
  addCallbacks_.clear();
  removeCallbacks_.clear();
}

} // namespace core
