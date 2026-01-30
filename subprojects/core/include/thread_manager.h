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
 * @brief Thread pool singleton for task management
 *
 * Manages worker threads and task distribution across the engine.
 * Supports dedicated threads for logical devices.
 */
class CORE_API ThreadManager {
public:
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
   * @brief Initialize the thread pool
   * @param workerCount Number of worker threads (0 = auto-detect based on
   * hardware)
   */
  void initialize(uint32_t workerCount = 0);

  /**
   * @brief Shutdown the thread pool
   */
  void shutdown();

  /**
   * @brief Submit a task to the thread pool
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
   * @brief Wait for all pending tasks to complete
   */
  void waitAll();

  /**
   * @brief Reserve a thread for a logical device
   * @return Thread ID for the reserved thread, or 0 if no threads available
   */
  void reserveDeviceThread(int reserve);

  /**
   * @brief Release a reserved device thread
   */
  void releaseDeviceThread(int release);

  /**
   * @brief Reset the thread pool to a new total amount of threads
   * @param threads Amount of worker threads to be allocated
   */
  void resetPoolSize(uint8_t threads = 2);

  /**
   * @brief Get the number of active worker threads
   */
  [[nodiscard]] uint32_t workerCount() const {
    return up_pool ? uint32_t(up_pool->get_thread_count()) : 0;
  }

  /**
   * @brief Get the number of reserved device threads
   */
  [[nodiscard]] uint32_t reservedCount() const { return _reservedWorkerCount; }

  /**
   * @brief Check if the thread pool is running
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

  const static BS::tp features = BS::tp::priority | BS::tp::pause;
  std::unique_ptr<BS::thread_pool<features>> up_pool;

  std::mutex _poolMutex;

  uint8_t _totalWorkerCount = 0;
  uint8_t _reservedWorkerCount = 0;
};

} // namespace core

#endif // THREAD_MANAGER_H_
