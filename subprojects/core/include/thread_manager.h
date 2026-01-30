#ifndef THREAD_MANAGER_H_
#define THREAD_MANAGER_H_

#include "core_export.h"
#include <BS_thread_pool.hpp>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <unordered_map>
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
 */
struct CORE_API ThreadManagerConfig {
  uint32_t totalThreads = 0; // 0 = auto-detect
  uint32_t loopThreads = 1;  // Threads reserved for main loops
  uint32_t gpuThreads = 0;   // Threads reserved for GPU operations
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

  // Task types
  template <typename T> using Task = std::function<T>;
  template <typename T> using TaskResult = std::future<T>;

  /**
   * @brief Initialize the thread pool with default configuration
   * @param workerCount Number of worker threads (0 = auto-detect based on
   * hardware)
   */
  void initialize(uint32_t workerCount = 0);

  /**
   * @brief Initialize with full configuration
   * @param config Thread manager configuration
   */
  void initialize(const ThreadManagerConfig &config);

  /**
   * @brief Shutdown the thread pool
   */
  void shutdown();

  // ========== Default Pool Access (Backward Compatible) ==========

  /**
   * @brief Submit a task to the default worker pool
   * @param task The task to execute
   * @return Future for task completion
   */
  template <typename F>
  auto submit(F &&task, BS::priority_t priority = BS::pr::normal)
      -> std::future<decltype(task())> {
    return up_pool->submit_task(std::forward<F>(task), priority);
  }

  /**
   * @brief Submit multiple tasks and wait for all to complete
   * @param tasks Vector of tasks to execute
   * @template T = type of function, R = type of return for the functions
   */
  template <typename T, typename R>
  auto submitBatch(const std::vector<Task<T>> &tasks,
                   BS::priority_t priority = BS::pr::normal)
      -> BS::multi_future<R> {
    return up_pool->submit_bulk(tasks, priority);
  }

  /**
   * @brief Wait for all pending tasks to complete in default pool
   */
  void waitAll();

  /**
   * @brief Reserve threads from the default pool for device use
   * @param reserve Number of threads to reserve
   * @deprecated Use createPool() with PoolType::GPU instead
   */
  void reserveDeviceThread(int reserve);

  /**
   * @brief Release reserved device threads back to the default pool
   * @param release Number of threads to release
   * @deprecated Use destroyPool() instead
   */
  void releaseDeviceThread(int release);

  /**
   * @brief Reset the default pool to a new total amount of threads
   * @param threads Amount of worker threads to be allocated
   */
  void resetPoolSize(uint8_t threads = 2);

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
    std::lock_guard<std::mutex> lock(_poolMutex);
    auto it = namedPools.find(name);
    if (it == namedPools.end()) {
      throw std::runtime_error("Thread pool '" + name + "' not found");
    }
    return it->second.pool->submit_task(std::forward<F>(task), priority);
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
   * @brief Get the number of active worker threads in default pool
   */
  [[nodiscard]] uint32_t workerCount() const {
    return up_pool ? uint32_t(up_pool->get_thread_count()) : 0;
  }

  /**
   * @brief Get the number of reserved device threads
   */
  [[nodiscard]] uint32_t reservedCount() const { return _reservedWorkerCount; }

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

  /**
   * @brief Check if the thread manager is running
   */
  [[nodiscard]] bool isRunning() const { return up_pool != nullptr; }

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

  // Default worker pool
  std::unique_ptr<BS::thread_pool<features>> up_pool;

  // Named pools registry
  std::unordered_map<std::string, PoolEntry> namedPools;

  // Current configuration
  ThreadManagerConfig currentConfig;

  mutable std::mutex _poolMutex;

  uint8_t _totalWorkerCount = 0;
  uint8_t _reservedWorkerCount = 0;
};

} // namespace core

#endif // THREAD_MANAGER_H_
