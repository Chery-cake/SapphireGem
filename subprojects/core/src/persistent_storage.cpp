#include "persistent_storage.h"
#include <mutex>
#include <stdexcept>

namespace core {

#ifdef ENGINE_DEBUG
static PersistentStorage *g_persistentStorageInstance = nullptr;
#endif

PersistentStorage &PersistentStorage::instance() {
#ifdef ENGINE_DEBUG
  if (g_persistentStorageInstance) {
    return *g_persistentStorageInstance;
  }
#endif
  static PersistentStorage instance;
#ifdef ENGINE_DEBUG
  g_persistentStorageInstance = &instance;
#endif
  return instance;
}

#ifdef ENGINE_DEBUG
void PersistentStorage::setInstance(PersistentStorage *inst) {
  g_persistentStorageInstance = inst;
}

PersistentStorage *PersistentStorage::getInstance() {
  return g_persistentStorageInstance;
}
#endif

PersistentStorage::PersistentStorage() = default;

PersistentStorage::~PersistentStorage() { shutdown(); }

bool PersistentStorage::initialize(size_t capacity) {
  std::lock_guard<std::mutex> lock(storageMutex_);

  if (allocator_) {
    return false; // Already initialized
  }

  try {
    allocator_ = std::make_unique<BumpAllocator>(capacity);
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

bool PersistentStorage::isInitialized() const {
  std::lock_guard<std::mutex> lock(storageMutex_);
  return allocator_ != nullptr;
}

bool PersistentStorage::exists(const std::string &name) const {
  std::lock_guard<std::mutex> lock(storageMutex_);
  return variables_.find(name) != variables_.end();
}

StoredVariableInfo PersistentStorage::getInfo(const std::string &name) const {
  std::lock_guard<std::mutex> lock(storageMutex_);

  auto it = variables_.find(name);
  if (it != variables_.end()) {
    return it->second;
  }
  return StoredVariableInfo{};
}

std::vector<std::string> PersistentStorage::getVariableNames() const {
  std::lock_guard<std::mutex> lock(storageMutex_);

  std::vector<std::string> names;
  names.reserve(variables_.size());
  for (const auto &pair : variables_) {
    names.push_back(pair.first);
  }
  return names;
}

size_t PersistentStorage::getBytesUsed() const {
  std::lock_guard<std::mutex> lock(storageMutex_);
  return allocator_ ? allocator_->bytes_allocated() : 0;
}

size_t PersistentStorage::getCapacity() const {
  std::lock_guard<std::mutex> lock(storageMutex_);
  return allocator_ ? allocator_->capacity() : 0;
}

void PersistentStorage::registerRecoveryCallback(const std::string &typeName,
                                                  RecoveryCallback callback) {
  std::lock_guard<std::mutex> lock(storageMutex_);
  recoveryCallbacks_[typeName] = std::move(callback);
}

void PersistentStorage::runRecoveryCallbacks() {
  std::lock_guard<std::mutex> lock(storageMutex_);

  for (auto &[name, info] : variables_) {
    auto it = recoveryCallbacks_.find(info.typeName);
    if (it != recoveryCallbacks_.end()) {
      it->second(info.address, info);
    }
  }
}

std::unordered_map<std::string, StoredVariableInfo>
PersistentStorage::exportRegistry() const {
  std::lock_guard<std::mutex> lock(storageMutex_);
  return variables_;
}

void PersistentStorage::importRegistry(
    const std::unordered_map<std::string, StoredVariableInfo> &registry) {
  std::lock_guard<std::mutex> lock(storageMutex_);
  variables_ = registry;
}

void PersistentStorage::shutdown() {
  std::lock_guard<std::mutex> lock(storageMutex_);
  // Note: We intentionally do NOT destroy objects in the allocator
  // because they may be needed for recovery. The allocator's destructor
  // will free the memory when the storage is truly done.
  variables_.clear();
  recoveryCallbacks_.clear();
  allocator_.reset();
}

} // namespace core
