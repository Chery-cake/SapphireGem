#ifndef HOT_RELOAD_MODULES_HPP_
#define HOT_RELOAD_MODULES_HPP_

#ifdef ENGINE_DEBUG

#include "core_export_struct.h"
#include "device_export_struct.h"
#include "hot_reload.h"
#include "window_export_struct.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <print>
#include <string>
#include <thread>
#include <vector>

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
   * @brief Initialize all module hot reloaders and load them in dependency order
   * @param exeDir Directory containing the executable
   * @return true if all modules loaded successfully
   */
  bool initialize(const std::string &exeDir) {
#ifdef _WIN32
    std::string corePath = exeDir + "/lib/cored.dll";
    std::string devicePath = exeDir + "/lib/deviced.dll";
    std::string windowPath = exeDir + "/lib/windowd.dll";
#elif __APPLE__
    std::string corePath = exeDir + "/lib/libcored.dylib";
    std::string devicePath = exeDir + "/lib/libdeviced.dylib";
    std::string windowPath = exeDir + "/lib/libwindowd.dylib";
#else
    std::string corePath = exeDir + "/lib/libcored.so";
    std::string devicePath = exeDir + "/lib/libdeviced.so";
    std::string windowPath = exeDir + "/lib/libwindowd.so";
#endif

    core_ = std::make_unique<HotReload>("core", corePath);
    device_ = std::make_unique<HotReload>("device", devicePath);
    window_ = std::make_unique<HotReload>("window", windowPath);

    // Dependency chain: core -> device -> window
    // When core reloads, device reloads, which cascades to window
    // When device reloads, window reloads
    core_->addDependent(device_.get());
    device_->addDependent(window_.get());

    // Initialize module state
    core_->setData(new coreState{nullptr, nullptr, nullptr});
    device_->setData(new deviceState{});
    window_->setData(new windowState{});

    // Register lifecycle callbacks for each module
    setupCoreCallbacks();
    setupDeviceCallbacks();
    setupWindowCallbacks();

    // Load modules in dependency order: core first, then device, then window
    if (!core_->load()) {
      std::print(stderr, "[ModuleReloadManager] Failed to load core module!\n");
      cleanupCoreState();
      return false;
    }

    if (!device_->load()) {
      std::print(stderr,
                 "[ModuleReloadManager] Failed to load device module!\n");
      return false;
    }

    if (!window_->load()) {
      std::print(stderr,
                 "[ModuleReloadManager] Failed to load window module!\n");
      return false;
    }

    std::print("[ModuleReloadManager] All modules loaded successfully\n");
    return true;
  }

  /**
   * @brief Start multi-threaded file monitoring for all modules
   *
   * Each module gets its own monitoring thread. A shared mutex
   * ensures reload operations are serialized.
   */
  void startMonitoring() {
    std::print("\n=== Starting Hot Reload Monitoring ===\n");

    // Core module monitor thread
    monitorThreads_.emplace_back([this](std::stop_token stoken) {
      while (!stoken.stop_requested()) {
        {
          std::lock_guard<std::mutex> lock(reloadMutex_);
          if (core_ && core_->checkAndReloadIfNeeded()) {
            std::print(
                ">>> Core module reloaded (dependents cascaded)! <<<\n\n");
          }
        }
        std::this_thread::sleep_for(std::chrono::seconds(5));
      }
    });

    // Device module monitor thread
    monitorThreads_.emplace_back([this](std::stop_token stoken) {
      while (!stoken.stop_requested()) {
        {
          std::lock_guard<std::mutex> lock(reloadMutex_);
          if (device_ && device_->checkAndReloadIfNeeded()) {
            std::print(
                ">>> Device module reloaded (dependents cascaded)! <<<\n\n");
          }
        }
        std::this_thread::sleep_for(std::chrono::seconds(5));
      }
    });

    // Window module monitor thread
    monitorThreads_.emplace_back([this](std::stop_token stoken) {
      while (!stoken.stop_requested()) {
        {
          std::lock_guard<std::mutex> lock(reloadMutex_);
          if (window_ && window_->checkAndReloadIfNeeded()) {
            std::print(">>> Window module reloaded! <<<\n\n");
          }
        }
        std::this_thread::sleep_for(std::chrono::seconds(5));
      }
    });
  }

  /**
   * @brief Stop all monitoring threads and release module resources
   *
   * Safe to call multiple times (idempotent).
   */
  void shutdown() {
    for (auto &t : monitorThreads_) {
      t.request_stop();
    }
    for (auto &t : monitorThreads_) {
      if (t.joinable()) {
        t.join();
      }
    }
    monitorThreads_.clear();

    // Modules are destroyed in reverse dependency order.
    // Guard each reset so double-shutdown is safe.
    if (window_)
      window_.reset();
    if (device_)
      device_.reset();
    if (core_)
      core_.reset();
  }

private:
  /**
   * @brief Cleanup a coreState, optionally calling shutdown on singletons.
   *
   * Static so it can be called from lambdas that only capture a raw pointer.
   * @param state     The coreState to clean up (may be nullptr).
   * @param callShutdown  If true, call shutdown() on each singleton before
   *                      deleting. Use true during normal destroy, false during
   *                      init-failure cleanup (singletons not fully started).
   */
  static void destroyCoreState(coreState *state, bool callShutdown) {
    if (!state)
      return;
    if (state->thread) {
      if (callShutdown)
        state->thread->shutdown();
      delete state->thread;
    }
    if (state->memory) {
      if (callShutdown)
        state->memory->shutdown();
      delete state->memory;
    }
    if (state->config) {
      if (callShutdown)
        state->config->shutdown();
      delete state->config;
    }
    delete state;
  }

  void setupCoreCallbacks() {
    // Capture a stable raw pointer — guaranteed valid for the lifetime of
    // the HotReload object.  unique_ptr::reset() nulls the smart pointer
    // BEFORE invoking ~HotReload, so callbacks must NOT go through core_.
    HotReload *corePtr = core_.get();

    corePtr->registerLoadCallback("core_load", [corePtr](void *data) {
      std::print("[ModuleReloadManager] Core library loaded\n");

      coreState *state = static_cast<coreState *>(data);
      auto fn =
          reinterpret_cast<void (*)(void *)>(corePtr->getSymbol("lib_on_load"));
      if (fn) {
        fn(state);
      } else {
        std::print(
            stderr,
            "[ModuleReloadManager] Could not find 'lib_on_load' in core\n");
      }
    });

    corePtr->registerUnloadCallback("core_unload", [corePtr](void *data) {
      std::print("[ModuleReloadManager] Core library unloading\n");

      coreState *state = static_cast<coreState *>(data);
      auto fn = reinterpret_cast<void (*)(void *)>(
          corePtr->getSymbol("lib_on_unload"));
      if (fn) {
        fn(state);
      }
    });

    corePtr->registerReloadCallback("core_reload", [corePtr](void *data) {
      std::print("[ModuleReloadManager] Core library reloading\n");

      coreState *state = static_cast<coreState *>(data);
      auto fn = reinterpret_cast<void (*)(void *)>(
          corePtr->getSymbol("lib_on_reload"));
      if (fn) {
        fn(state);
      }
    });

    corePtr->registerDestroyCallback("core_cleanup", [corePtr](void *data) {
      std::print("[ModuleReloadManager] Core library cleanup\n");

      auto fn = reinterpret_cast<void (*)(void *)>(
          corePtr->getSymbol("lib_on_destroy"));
      if (fn) {
        fn(data);
      } else {
        std::print(stderr, "[ModuleReloadManager] Warning: lib_on_destroy not "
                           "found, manual cleanup\n");
        destroyCoreState(static_cast<coreState *>(data), true);
      }
      corePtr->setData(nullptr);
    });
  }

  void setupDeviceCallbacks() {
    HotReload *devicePtr = device_.get();

    devicePtr->registerLoadCallback("device_load", [devicePtr](void *data) {
      std::print("[ModuleReloadManager] Device library loaded\n");

      auto fn = reinterpret_cast<void (*)(void *)>(
          devicePtr->getSymbol("lib_on_load"));
      if (fn) {
        fn(data);
      }
    });

    devicePtr->registerUnloadCallback(
        "device_unload", [devicePtr](void *data) {
          std::print("[ModuleReloadManager] Device library unloading\n");

          auto fn = reinterpret_cast<void (*)(void *)>(
              devicePtr->getSymbol("lib_on_unload"));
          if (fn) {
            fn(data);
          }
        });

    devicePtr->registerReloadCallback(
        "device_reload", [devicePtr](void *data) {
          std::print("[ModuleReloadManager] Device library reloading\n");

          auto fn = reinterpret_cast<void (*)(void *)>(
              devicePtr->getSymbol("lib_on_reload"));
          if (fn) {
            fn(data);
          }
        });

    devicePtr->registerDestroyCallback(
        "device_cleanup", [devicePtr](void *data) {
          std::print("[ModuleReloadManager] Device library cleanup\n");

          auto fn = reinterpret_cast<void (*)(void *)>(
              devicePtr->getSymbol("lib_on_destroy"));
          if (fn) {
            fn(data);
          } else {
            delete static_cast<deviceState *>(data);
          }
          devicePtr->setData(nullptr);
        });
  }

  void setupWindowCallbacks() {
    HotReload *windowPtr = window_.get();

    windowPtr->registerLoadCallback("window_load", [windowPtr](void *data) {
      std::print("[ModuleReloadManager] Window library loaded\n");

      auto fn = reinterpret_cast<void (*)(void *)>(
          windowPtr->getSymbol("lib_on_load"));
      if (fn) {
        fn(data);
      }
    });

    windowPtr->registerUnloadCallback(
        "window_unload", [windowPtr](void *data) {
          std::print("[ModuleReloadManager] Window library unloading\n");

          auto fn = reinterpret_cast<void (*)(void *)>(
              windowPtr->getSymbol("lib_on_unload"));
          if (fn) {
            fn(data);
          }
        });

    windowPtr->registerReloadCallback(
        "window_reload", [windowPtr](void *data) {
          std::print("[ModuleReloadManager] Window library reloading\n");

          auto fn = reinterpret_cast<void (*)(void *)>(
              windowPtr->getSymbol("lib_on_reload"));
          if (fn) {
            fn(data);
          }
        });

    windowPtr->registerDestroyCallback(
        "window_cleanup", [windowPtr](void *data) {
          std::print("[ModuleReloadManager] Window library cleanup\n");

          auto fn = reinterpret_cast<void (*)(void *)>(
              windowPtr->getSymbol("lib_on_destroy"));
          if (fn) {
            fn(data);
          } else {
            delete static_cast<windowState *>(data);
          }
          windowPtr->setData(nullptr);
        });
  }

  /**
   * @brief Cleanup core state on initialization failure.
   *
   * Only called from initialize() where core_ is guaranteed valid.
   * Does NOT call shutdown() on singletons — they were never fully started.
   */
  void cleanupCoreState() {
    destroyCoreState(static_cast<coreState *>(core_->getData()), false);
    core_->setData(nullptr);
  }

  std::unique_ptr<HotReload> core_;
  std::unique_ptr<HotReload> device_;
  std::unique_ptr<HotReload> window_;
  std::vector<std::jthread> monitorThreads_;
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
