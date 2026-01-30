#ifndef MEMORY_MANAGER_H_
#define MEMORY_MANAGER_H_

#include "bump_allocator.h"
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {

/**
 * @brief Manages memory allocators for the engine
 *
 * Supports multiple named persistent and frame (temporary) allocators.
 * The default allocators ("default") are used for backward compatibility.
 */
class CORE_API MemoryManager {
public:
  static MemoryManager &instance();

  // Initialize the memory manager with default allocator sizes
  void initialize(size_t persistentSize = 10 * 1024 * 1024, // 10MB
                  size_t frameSize = 5 * 1024 * 1024);      // 5MB

  // ========== Default Allocator Access (Backward Compatible) ==========

  // Get default allocators
  BumpAllocator &getPersistentAllocator();
  BumpAllocator &getFrameAllocator();

  // Reset default frame allocator (call at start of each frame)
  void resetFrameAllocator();

  // Get default allocation statistics
  size_t getPersistentBytesAllocated() const;
  size_t getFrameBytesAllocated() const;

  // ========== Named Allocator Management ==========

  /**
   * @brief Create a named persistent allocator
   * @param name Unique name for the allocator
   * @param size Size in bytes for the allocator
   * @return Reference to the created allocator
   * @throws std::runtime_error if allocator already exists
   */
  BumpAllocator &createPersistentAllocator(const std::string &name,
                                           size_t size);

  /**
   * @brief Create a named frame (temporary) allocator
   * @param name Unique name for the allocator
   * @param size Size in bytes for the allocator
   * @return Reference to the created allocator
   * @throws std::runtime_error if allocator already exists
   */
  BumpAllocator &createFrameAllocator(const std::string &name, size_t size);

  /**
   * @brief Get a named persistent allocator
   * @param name Name of the allocator
   * @return Reference to the allocator
   * @throws std::runtime_error if allocator doesn't exist
   */
  BumpAllocator &getPersistentAllocator(const std::string &name);

  /**
   * @brief Get a named frame allocator
   * @param name Name of the allocator
   * @return Reference to the allocator
   * @throws std::runtime_error if allocator doesn't exist
   */
  BumpAllocator &getFrameAllocator(const std::string &name);

  /**
   * @brief Check if a named persistent allocator exists
   * @param name Name of the allocator
   * @return true if allocator exists
   */
  bool hasPersistentAllocator(const std::string &name) const;

  /**
   * @brief Check if a named frame allocator exists
   * @param name Name of the allocator
   * @return true if allocator exists
   */
  bool hasFrameAllocator(const std::string &name) const;

  /**
   * @brief Destroy a named persistent allocator
   * @param name Name of the allocator to destroy
   * @return true if allocator was destroyed, false if not found
   */
  bool destroyPersistentAllocator(const std::string &name);

  /**
   * @brief Destroy a named frame allocator
   * @param name Name of the allocator to destroy
   * @return true if allocator was destroyed, false if not found
   */
  bool destroyFrameAllocator(const std::string &name);

  /**
   * @brief Reset a specific named frame allocator
   * @param name Name of the allocator to reset
   */
  void resetFrameAllocator(const std::string &name);

  /**
   * @brief Reset all frame allocators (including default)
   */
  void resetAllFrameAllocators();

  /**
   * @brief Get bytes allocated for a named persistent allocator
   * @param name Name of the allocator
   * @return Bytes allocated, or 0 if allocator doesn't exist
   */
  size_t getPersistentBytesAllocated(const std::string &name) const;

  /**
   * @brief Get bytes allocated for a named frame allocator
   * @param name Name of the allocator
   * @return Bytes allocated, or 0 if allocator doesn't exist
   */
  size_t getFrameBytesAllocated(const std::string &name) const;

  /**
   * @brief Get list of all persistent allocator names
   * @return Vector of allocator names
   */
  std::vector<std::string> getPersistentAllocatorNames() const;

  /**
   * @brief Get list of all frame allocator names
   * @return Vector of allocator names
   */
  std::vector<std::string> getFrameAllocatorNames() const;

  void shutdown();

  // Disable copying
  MemoryManager(const MemoryManager &) = delete;
  MemoryManager &operator=(const MemoryManager &) = delete;

#ifdef ENGINE_DEBUG
  // Hot reload support: set/get the singleton instance
  static void setInstance(MemoryManager *inst);
  static MemoryManager *getInstance();

  // In debug mode, allow direct instantiation for hot reload
  MemoryManager() = default;
  ~MemoryManager() = default;
#else
private:
  MemoryManager() = default;
  ~MemoryManager() = default;
#endif

private:
  static constexpr const char *DEFAULT_ALLOCATOR_NAME = "default";

  // Named allocator registries
  std::unordered_map<std::string, std::unique_ptr<BumpAllocator>>
      persistentAllocators;
  std::unordered_map<std::string, std::unique_ptr<BumpAllocator>>
      frameAllocators;

  mutable std::mutex allocatorMutex;
};

} // namespace core

#endif // MEMORY_MANAGER_H_
