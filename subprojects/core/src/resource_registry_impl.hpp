#pragma once
#include "resource_registry.h"
#include <algorithm>
#include <iterator>
#include <memory>
#include <mutex>
#include <vector>

namespace core {

template <typename Tag, typename Asset>
bool ResourceRegistry<Tag, Asset>::add(const Tag *tag,
                                       std::unique_ptr<Asset> asset) {
  Asset *assetPtr = nullptr;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto [it, ins] = assets_.try_emplace(tag, std::move(asset));
    if (!ins) {
      return false;
    }
    assetPtr = it->second.get();
  }

  assetAdded_.emit(tag, assetPtr);

  return true;
}

template <typename Tag, typename Asset>
template <typename... Args>
bool ResourceRegistry<Tag, Asset>::emplace(const Tag *tag, Args &&...args) {
  return add(tag, std::make_unique<Asset>(*tag, std::forward<Args>(args)...));
}

template <typename Tag, typename Asset>
bool ResourceRegistry<Tag, Asset>::set(const Tag *tag,
                                       std::unique_ptr<Asset> asset) {
  Asset *oldPtr = nullptr;
  Asset *newPtr = nullptr;
  bool wasNew = false;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = assets_.find(tag);
    if (it != assets_.end()) {
      oldPtr = it->second.get();

    } else {
      wasNew = true;
    }

    assets_[tag] = std::move(asset);
    newPtr = assets_[tag].get();
  }

  // Invoke remove callbacks first if replacing
  if (oldPtr) {
    assetRemoved_.emit(tag, oldPtr);
  }

  // Then invoke add callbacks
  assetAdded_.emit(tag, newPtr);

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
  std::unique_ptr<Asset> extracted;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = assets_.find(tag);
    if (it == assets_.end()) {
      return false;
    }
    extracted = std::move(it->second);
    assets_.erase(it);
  }

  assetRemoved_.emit(tag, extracted.get());

  return true;
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
    assetRemoved_.emit(tag, assetPtr);
  }

  return result;
}

template <typename Tag, typename Asset>
template <typename Func>
void ResourceRegistry<Tag, Asset>::forEach(Func &&func) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ranges::for_each(assets_, [&func](const auto &pair) {
    func(pair.first, pair.second.get());
  });
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
  decltype(assets_) localAssets;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    localAssets.swap(assets_);
  }

  std::ranges::for_each(localAssets, [&](const auto &pair) {
    assetRemoved_.emit(pair.first, pair.second.get());
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
signal::Signal<typename ResourceRegistry<Tag, Asset>::SignalCall>::ConnectResult
ResourceRegistry<Tag, Asset>::onAdd(SignalSlot signalSlot) {
  return assetAdded_.connect(std::move(signalSlot));
}

template <typename Tag, typename Asset>
signal::Signal<typename ResourceRegistry<Tag, Asset>::SignalCall>::ConnectResult
ResourceRegistry<Tag, Asset>::onRemove(SignalSlot signalSlot) {
  return assetRemoved_.connect(std::move(signalSlot));
}

template <typename Tag, typename Asset>
void ResourceRegistry<Tag, Asset>::clearCallbacks() {
  std::lock_guard<std::mutex> lock(mutex_);
  assetAdded_.clear();
  assetRemoved_.clear();
}

} // namespace core
