#ifndef HOT_RELOAD_MODULES_HPP_
#define HOT_RELOAD_MODULES_HPP_

#include <string>

#ifdef ENGINE_DEBUG

#include "core_export_struct.h"
#include "hot_reload.h"
#include <atomic>
#include <cstdint>
#include <thread>

/**
 * @brief Manages hot reloading for all engine modules with dependency cascading
 *
 * Sets up HotReload instances for core, device, and window modules.
 * Dependency chain: core -> device -> window
 *   - Core reload triggers device reload, which triggers window reload
 *   - Device reload triggers window reload
 *   - Window reload does not trigger any other reload
 *
 * Monitoring is multi-threaded with self-managed threads (does not use
 * core::ThreadManager pools). Each module has its own monitoring thread
 * that checks for library file changes independently. A shared mutex
 * serializes actual reload operations to prevent concurrent reloads.
 */
class ModuleReloadManager {
public:
  ModuleReloadManager() = default;
  ~ModuleReloadManager() { shutdown(); }

  ModuleReloadManager(const ModuleReloadManager &) = delete;
  ModuleReloadManager &operator=(const ModuleReloadManager &) = delete;
  ModuleReloadManager(ModuleReloadManager &&) = delete;
  ModuleReloadManager &operator=(ModuleReloadManager &&) = delete;

  /**
   * @brief Initialize all module hot reloaders and load them in dependency
   * order
   * @param exeDir Directory containing the executable
   * @return true if all modules loaded successfully
   */
  bool initialize(const std::string &exeDir);

  /**
   * @brief Start multi-threaded file monitoring for all modules
   *
   * Each module gets its own monitoring thread. A shared mutex
   * ensures reload operations are serialized.
   */
  void startMonitoring();

  /**
   * @brief Stop all monitoring threads and release module resources
   *
   * Safe to call multiple times (idempotent).
   */
  void shutdown();

private:
  /**
   * @brief Cleanup a coreState, optionally calling shutdown on singletons.
   *
   * Static so it can be called from lambdas that only capture a raw pointer.
   * @param state     The coreState to clean up (may be nullptr).
   * @param callShutdown  If true, call shutdown() on each singleton before
   *                      deleting. Use true during normal destroy, false
   * during init-failure cleanup (singletons not fully started).
   */
  static void destroyCoreState(coreState *state, bool callShutdown);

  void setupCoreCallbacks();
  void setupDeviceCallbacks();
  void setupWindowCallbacks();

  /**
   * @brief Cleanup core state on initialization failure.
   *
   * Only called from initialize() where core_ is guaranteed valid.
   * Does NOT call shutdown() on singletons — they were never fully started.
   */
  void cleanupCoreState();

  static constexpr uint8_t monitor_delay_ = 10;

  std::unique_ptr<HotReload> core_;
  std::unique_ptr<HotReload> device_;
  std::unique_ptr<HotReload> window_;
  std::atomic<bool> stopRequested_{false};
  std::vector<std::thread> monitorThreads_;
  std::mutex reloadMutex_;
};

#else // !ENGINE_DEBUG

// Release mode: no-op stub
class ModuleReloadManager {
public:
  bool initialize(const std::string &) { return true; }
  void startMonitoring() {}
  void shutdown() {}
};

#endif // ENGINE_DEBUG

#endif // HOT_RELOAD_MODULES_HPP_
