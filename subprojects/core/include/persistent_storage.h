#ifndef PERSISTENT_STORAGE_H_
#define PERSISTENT_STORAGE_H_

#include "bump_allocator.h"
#include "core_export.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core {

/**
 * @brief Metadata for a stored variable in persistent storage
 *
 * This structure holds information needed to recover a variable
 * after a hot reload.
 */
struct CORE_API StoredVariableInfo {
  std::string name;        // Unique name for the variable
  void *address;           // Current address in memory
  size_t size;             // Size of the allocation
  size_t alignment;        // Alignment requirement
  std::string typeName;    // Type name for debugging/recovery
  uint64_t version;        // Version number for compatibility checking
};

/**
 * @brief Recovery callback type
 *
 * Called during recovery to reconstruct an object at the given address.
 * @param address The memory address where the object is stored
 * @param info The stored variable information
 * @return true if recovery was successful
 */
using RecoveryCallback = std::function<bool(void *address, const StoredVariableInfo &info)>;

/**
 * @brief Persistent storage manager with hot-reload recovery support
 *
 * This class extends the basic bump allocator to support:
 * - Named variable storage with metadata
 * - Recovery of variables after hot reload
 * - Type-safe allocation with registration
 *
 * Usage pattern for hot reload:
 * 1. First load: allocate and register variables with store()
 * 2. Before unload: metadata is preserved (not the allocator itself)
 * 3. After reload: use recover() to get pointers to existing allocations
 *
 * Example:
 * @code
 *   // First load
 *   auto& storage = PersistentStorage::instance();
 *   MyClass* ptr = storage.store<MyClass>("my_object", MyClass(args...));
 *
 *   // After hot reload
 *   MyClass* recovered = storage.recover<MyClass>("my_object");
 *   if (recovered) {
 *     // Object is still valid at same address
 *   }
 * @endcode
 */
class CORE_API PersistentStorage {
public:
  // Singleton access
  static PersistentStorage &instance();

  // Delete copy and move operations
  PersistentStorage(const PersistentStorage &) = delete;
  PersistentStorage &operator=(const PersistentStorage &) = delete;
  PersistentStorage(PersistentStorage &&) = delete;
  PersistentStorage &operator=(PersistentStorage &&) = delete;

  /**
   * @brief Initialize the persistent storage with a given capacity
   * @param capacity Size in bytes for the storage
   * @return true if initialization was successful
   */
  bool initialize(size_t capacity);

  /**
   * @brief Check if storage has been initialized
   * @return true if initialized
   */
  bool isInitialized() const;

  /**
   * @brief Store a new variable in persistent storage
   *
   * @tparam T Type of the variable to store
   * @param name Unique name for the variable
   * @param value The value to copy-construct into storage
   * @param version Optional version number for compatibility
   * @return Pointer to the stored variable, or nullptr on failure
   */
  template <typename T>
  T *store(const std::string &name, const T &value, uint64_t version = 1) {
    std::lock_guard<std::mutex> lock(storageMutex_);

    if (!allocator_) {
      return nullptr;
    }

    // Check if already exists
    auto it = variables_.find(name);
    if (it != variables_.end()) {
      return nullptr; // Already exists
    }

    // Allocate memory
    void *addr = allocator_->allocate(sizeof(T), alignof(T));
    if (!addr) {
      return nullptr;
    }

    // Construct the object
    T *ptr = new (addr) T(value);

    // Register metadata
    StoredVariableInfo info;
    info.name = name;
    info.address = addr;
    info.size = sizeof(T);
    info.alignment = alignof(T);
    info.typeName = typeid(T).name();
    info.version = version;

    variables_[name] = info;

    return ptr;
  }

  /**
   * @brief Store a new variable using in-place construction
   *
   * @tparam T Type of the variable to store
   * @tparam Args Constructor argument types
   * @param name Unique name for the variable
   * @param version Version number for compatibility
   * @param args Arguments to forward to T's constructor
   * @return Pointer to the stored variable, or nullptr on failure
   */
  template <typename T, typename... Args>
  T *emplace(const std::string &name, uint64_t version, Args &&...args) {
    std::lock_guard<std::mutex> lock(storageMutex_);

    if (!allocator_) {
      return nullptr;
    }

    // Check if already exists
    auto it = variables_.find(name);
    if (it != variables_.end()) {
      return nullptr; // Already exists
    }

    // Allocate memory
    void *addr = allocator_->allocate(sizeof(T), alignof(T));
    if (!addr) {
      return nullptr;
    }

    // Construct the object
    T *ptr = new (addr) T(std::forward<Args>(args)...);

    // Register metadata
    StoredVariableInfo info;
    info.name = name;
    info.address = addr;
    info.size = sizeof(T);
    info.alignment = alignof(T);
    info.typeName = typeid(T).name();
    info.version = version;

    variables_[name] = info;

    return ptr;
  }

  /**
   * @brief Recover a previously stored variable after hot reload
   *
   * @tparam T Type of the variable to recover
   * @param name Name of the variable
   * @return Pointer to the variable, or nullptr if not found
   */
  template <typename T> T *recover(const std::string &name) {
    std::lock_guard<std::mutex> lock(storageMutex_);

    auto it = variables_.find(name);
    if (it == variables_.end()) {
      return nullptr;
    }

    // Validate size and alignment
    if (it->second.size != sizeof(T) || it->second.alignment != alignof(T)) {
      return nullptr; // Type mismatch
    }

    return static_cast<T *>(it->second.address);
  }

  /**
   * @brief Get or create a variable
   *
   * If the variable exists, returns the existing pointer.
   * If not, creates a new one using the provided factory.
   *
   * @tparam T Type of the variable
   * @param name Variable name
   * @param factory Function to create the value if needed
   * @param version Version number for new variables
   * @return Pointer to the variable (existing or new)
   */
  template <typename T>
  T *getOrCreate(const std::string &name, std::function<T()> factory,
                 uint64_t version = 1) {
    // Try to recover first
    T *existing = recover<T>(name);
    if (existing) {
      return existing;
    }

    // Create new
    return store<T>(name, factory(), version);
  }

  /**
   * @brief Check if a variable exists
   * @param name Variable name
   * @return true if the variable exists
   */
  bool exists(const std::string &name) const;

  /**
   * @brief Get information about a stored variable
   * @param name Variable name
   * @return StoredVariableInfo, or empty info if not found
   */
  StoredVariableInfo getInfo(const std::string &name) const;

  /**
   * @brief Get all stored variable names
   * @return Vector of variable names
   */
  std::vector<std::string> getVariableNames() const;

  /**
   * @brief Get total bytes used in storage
   * @return Bytes allocated
   */
  size_t getBytesUsed() const;

  /**
   * @brief Get storage capacity
   * @return Total capacity in bytes
   */
  size_t getCapacity() const;

  /**
   * @brief Register a recovery callback for a type
   *
   * The callback is called during recovery to reconstruct vtables
   * or reinitialize objects that need special handling.
   *
   * @param typeName The type name (use typeid(T).name())
   * @param callback The recovery callback
   */
  void registerRecoveryCallback(const std::string &typeName,
                                RecoveryCallback callback);

  /**
   * @brief Run recovery callbacks for all stored variables
   *
   * Call this after hot reload to reinitialize objects that
   * have registered recovery callbacks.
   */
  void runRecoveryCallbacks();

  /**
   * @brief Export variable registry for preservation during reload
   *
   * @return Map of variable metadata (can be stored externally)
   */
  std::unordered_map<std::string, StoredVariableInfo> exportRegistry() const;

  /**
   * @brief Import variable registry after reload
   *
   * @param registry Previously exported registry
   */
  void importRegistry(
      const std::unordered_map<std::string, StoredVariableInfo> &registry);

  /**
   * @brief Shutdown and release all storage
   */
  void shutdown();

#ifdef ENGINE_DEBUG
  // Hot reload support: set/get the singleton instance
  static void setInstance(PersistentStorage *inst);
  static PersistentStorage *getInstance();

  // In debug mode, allow direct instantiation for hot reload
  PersistentStorage();
  ~PersistentStorage();
#else
private:
  PersistentStorage();
  ~PersistentStorage();
#endif

private:
  std::unique_ptr<BumpAllocator> allocator_;
  std::unordered_map<std::string, StoredVariableInfo> variables_;
  std::unordered_map<std::string, RecoveryCallback> recoveryCallbacks_;
  mutable std::mutex storageMutex_;
};

} // namespace core

#endif // PERSISTENT_STORAGE_H_
