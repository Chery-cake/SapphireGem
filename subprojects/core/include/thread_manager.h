#ifndef THREAD_MANAGER_H_
#define THREAD_MANAGER_H_

#include "core_export.h"
#include <BS_thread_pool.hpp>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <sys/types.h>
#include <vector>

namespace core {

/**
 * @brief Pool type for specialized thread pools
 */
enum class PoolType : uint8_t {
  Worker,   // General worker pool for tasks
  MainLoop, // Pool for main loop callbacks (SDL, Steamworks, draw frame)
  GPU       // Pool reserved for GPU operations
};

/**
 * @brief Configuration for a thread pool
 */
struct CORE_API ThreadPoolConfig {
  std::string name;
  PoolType type = PoolType::Worker;
  uint32_t threadCount = 1;

  // Optional callback when pool is reconfigured (for GPU implementations)
  std::function<void(uint32_t newThreadCount)> onReconfigure = nullptr;
};

/**
 * @brief Global configuration for thread allocation
 *
 * Note: loopThreads and gpuThreads values are subtracted from totalThreads
 * to calculate the default worker pool size. These values represent thread
 * budget that should be allocated to specialized pools created via
 * createPool(). The specialized pools must still be created manually.
 */
struct CORE_API ThreadManagerConfig {
  uint32_t totalThreads = 0; // 0 = auto-detect
  uint32_t loopThreads = 1;  // Thread budget for main loop pools
  uint32_t gpuThreads = 0;   // Thread budget for GPU operation pools
};

/**
 * @brief Thread pool singleton for task management
 *
 * Manages worker threads and task distribution across the engine.
 * Supports dedicated thread pools for different purposes:
 * - Worker pools for general task distribution
 * - MainLoop pools for SDL, Steamworks, draw frame callbacks
 * - GPU pools for GPU-specific operations
 */
class CORE_API ThreadManager {
public:
  // Thread pool features configuration
  static constexpr BS::tp features = BS::tp::priority | BS::tp::pause;

  // Singleton access
  static ThreadManager &instance();

  // Delete copy and move operations
  ThreadManager(const ThreadManager &) = delete;
  ThreadManager &operator=(const ThreadManager &) = delete;
  ThreadManager(ThreadManager &&) = delete;
  ThreadManager &operator=(ThreadManager &&) = delete;

  /**
   * @brief Shutdown the thread pool
   */
  void shutdown();

  // ========== Named Pool Management ==========

  /**
   * @brief Create a named thread pool
   * @param config Pool configuration
   * @return true if pool was created, false if it already exists
   */
  bool createPool(const ThreadPoolConfig &config);

  /**
   * @brief Get a named pool for submitting tasks
   * @param name Pool name
   * @return Pointer to the pool, or nullptr if not found
   */
  BS::thread_pool<features> *getPool(const std::string &name);

  /**
   * @brief Check if a named pool exists
   * @param name Pool name
   * @return true if pool exists
   */
  bool hasPool(const std::string &name) const;

  /**
   * @brief Destroy a named pool
   * @param name Pool name
   * @return true if pool was destroyed, false if not found
   */
  bool destroyPool(const std::string &name);

  /**
   * @brief Resize a named pool
   * @param name Pool name
   * @param newThreadCount New thread count
   * @return true if pool was resized, false if not found
   */
  bool resizePool(const std::string &name, uint32_t newThreadCount);

  /**
   * @brief Submit a task to a named pool
   * @param name Pool name
   * @param task The task to execute
   * @param priority Task priority
   * @return Future for task completion
   */
  template <typename F>
  auto submitTo(const std::string &name, F &&task,
                BS::priority_t priority = BS::pr::normal)
      -> std::future<decltype(task())> {
    std::lock_guard<std::mutex> lock(threadMutex_);
    auto it = threadPools_.find(name);
    if (it == threadPools_.end()) {
      throw std::runtime_error("Thread pool '" + name + "' not found");
    }
    return it->second.pool->submit_task(std::forward<F>(task), priority);
  }

  /**
   * @brief Submit bulk of tasks to a named pool
   * @param name Pool name
   * @param bulk of tasks to execute
   * @param priority Task priority
   * @return Future for bulk completion
   */
  template <typename F, typename R>
  auto submitBulkTo(const std::string &name, const std::vector<F> &bulk,
                    BS::priority_t priority = BS::pr::normal)
      -> BS::multi_future<R> {
    std::lock_guard<std::mutex> lock(threadMutex_);
    auto it = threadPools_.find(name);
    if (it == threadPools_.end()) {
      throw std::runtime_error("Thread pool '" + name + "' not found");
    }
    return it->second.pool->submit_bulk(bulk, priority);
  }

  /**
   * @brief Wait for all pending tasks in a named pool
   * @param name Pool name
   */
  void waitAll(const std::string &name);

  /**
   * @brief Wait for all pools to complete their tasks
   */
  void waitAllPools();

  /**
   * @brief Get list of all pool names
   * @return Vector of pool names
   */
  std::vector<std::string> getPoolNames() const;

  /**
   * @brief Get pool configuration by name
   * @param name Pool name
   * @return Pool configuration, or default config if not found
   */
  ThreadPoolConfig getPoolConfig(const std::string &name) const;

  // ========== Configuration Management ==========

  /**
   * @brief Apply a new configuration (resizes/redistributes threads)
   * @param config New configuration
   *
   * This will notify GPU pools via their onReconfigure callback
   * so GPU implementations can handle the change.
   */
  void applyConfig(const ThreadManagerConfig &config);

  /**
   * @brief Get current configuration
   * @return Current thread manager configuration
   */
  ThreadManagerConfig getConfig() const;

  // ========== Statistics ==========

  /**
   * @brief Get the number of reserved device threads
   */
  [[nodiscard]] uint32_t reservedCount() const { return reservedWorkerCount_; }

  /**
   * @brief Get thread count for a named pool
   * @param name Pool name
   * @return Thread count, or 0 if pool not found
   */
  [[nodiscard]] uint32_t getPoolThreadCount(const std::string &name) const;

  /**
   * @brief Get total threads across all pools
   * @return Total thread count
   */
  [[nodiscard]] uint32_t totalThreadCount() const;

#ifdef ENGINE_DEBUG
  // Hot reload support: set/get the singleton instance
  static void setInstance(ThreadManager *inst);
  static ThreadManager *getInstance();
  // In debug mode, allow direct instantiation for hot reload
  ThreadManager();
  ~ThreadManager();
#else
private:
  ThreadManager();
  ~ThreadManager();
#endif

private:
  // Internal pool entry
  struct PoolEntry {
    std::unique_ptr<BS::thread_pool<features>> pool;
    ThreadPoolConfig config;
  };

  // Named pools registry
  std::unordered_map<std::string, PoolEntry> threadPools_;

  // Current configuration
  ThreadManagerConfig currentConfig_;

  mutable std::mutex threadMutex_;

  uint8_t totalWorkerCount_ = 0;
  uint8_t reservedWorkerCount_ = 0;
};

} // namespace core

#endif // THREAD_MANAGER_H_
