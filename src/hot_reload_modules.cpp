#include "hot_reload_modules.h"

#ifdef ENGINE_DEBUG

#include "device_export_struct.h"
#include "window_export_struct.h"
#include "print_compat.h"

bool ModuleReloadManager::initialize(const std::string &exeDir) {
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

  // Load modules in dependency order: core first, then device, then
  // window
  if (!core_->load()) {
    std::println(stderr, "[ModuleReloadManager] Failed to load core module!");
    cleanupCoreState();
    return false;
  }

  if (!device_->load()) {
    std::println(stderr, "[ModuleReloadManager] Failed to load device module!");
    return false;
  }

  if (!window_->load()) {
    std::println(stderr, "[ModuleReloadManager] Failed to load window module!");
    return false;
  }

  std::println("[ModuleReloadManager] All modules loaded successfully");
  return true;
}

void ModuleReloadManager::startMonitoring() {
  std::println("\n=== Starting Hot Reload Monitoring ===");

  stopRequested_.store(false);

  // Core module monitor thread
  monitorThreads_.emplace_back([this]() {
    while (!stopRequested_.load()) {
      {
        std::lock_guard<std::mutex> lock(reloadMutex_);
        if (core_ && core_->checkAndReloadIfNeeded()) {
          std::println(">>> Core module reloaded (dependents "
                       "cascaded)! <<<\n");
        }
      }
      std::this_thread::sleep_for(std::chrono::seconds(monitor_delay_));
    }
  });

  // Device module monitor thread
  monitorThreads_.emplace_back([this]() {
    while (!stopRequested_.load()) {
      {
        std::lock_guard<std::mutex> lock(reloadMutex_);
        if (device_ && device_->checkAndReloadIfNeeded()) {
          std::println(">>> Device module reloaded (dependents "
                       "cascaded)! <<<\n");
        }
      }
      std::this_thread::sleep_for(std::chrono::seconds(monitor_delay_));
    }
  });

  // Window module monitor thread
  monitorThreads_.emplace_back([this]() {
    while (!stopRequested_.load()) {
      {
        std::lock_guard<std::mutex> lock(reloadMutex_);
        if (window_ && window_->checkAndReloadIfNeeded()) {
          std::println(">>> Window module reloaded! <<<\n");
        }
      }
      std::this_thread::sleep_for(std::chrono::seconds(monitor_delay_));
    }
  });
}

void ModuleReloadManager::shutdown() {
  stopRequested_.store(true);
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

void ModuleReloadManager::destroyCoreState(coreState *state,
                                           bool callShutdown) {
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

void ModuleReloadManager::setupCoreCallbacks() {
  // Capture a stable raw pointer — guaranteed valid for the lifetime of
  // the HotReload object.  unique_ptr::reset() nulls the smart pointer
  // BEFORE invoking ~HotReload, so callbacks must NOT go through core_.
  HotReload *corePtr = core_.get();

  corePtr->registerLoadCallback("core_load", [corePtr](void *data) {
    std::println("[ModuleReloadManager] Core library loaded");

    coreState *state = static_cast<coreState *>(data);
    auto fn =
        reinterpret_cast<void (*)(void *)>(corePtr->getSymbol("lib_on_load"));
    if (fn) {
      fn(state);
    } else {
      std::println(stderr, "[ModuleReloadManager] Could not find "
                           "'lib_on_load' in core");
    }
  });

  corePtr->registerUnloadCallback("core_unload", [corePtr](void *data) {
    std::println("[ModuleReloadManager] Core library unloading");

    coreState *state = static_cast<coreState *>(data);
    auto fn =
        reinterpret_cast<void (*)(void *)>(corePtr->getSymbol("lib_on_unload"));
    if (fn) {
      fn(state);
    }
  });

  corePtr->registerReloadCallback("core_reload", [corePtr](void *data) {
    std::println("[ModuleReloadManager] Core library reloading");

    coreState *state = static_cast<coreState *>(data);
    auto fn =
        reinterpret_cast<void (*)(void *)>(corePtr->getSymbol("lib_on_reload"));
    if (fn) {
      fn(state);
    }
  });

  corePtr->registerDestroyCallback("core_cleanup", [corePtr](void *data) {
    std::println("[ModuleReloadManager] Core library cleanup");

    auto fn = reinterpret_cast<void (*)(void *)>(
        corePtr->getSymbol("lib_on_destroy"));
    if (fn) {
      fn(data);
    } else {
      std::println(stderr, "[ModuleReloadManager] Warning: lib_on_destroy not "
                           "found, manual cleanup");
      destroyCoreState(static_cast<coreState *>(data), true);
    }
    corePtr->setData(nullptr);
  });
}

void ModuleReloadManager::setupDeviceCallbacks() {
  HotReload *devicePtr = device_.get();

  devicePtr->registerLoadCallback("device_load", [devicePtr](void *data) {
    std::println("[ModuleReloadManager] Device library loaded");

    auto fn =
        reinterpret_cast<void (*)(void *)>(devicePtr->getSymbol("lib_on_load"));
    if (fn) {
      fn(data);
    }
  });

  devicePtr->registerUnloadCallback("device_unload", [devicePtr](void *data) {
    std::println("[ModuleReloadManager] Device library unloading");

    auto fn = reinterpret_cast<void (*)(void *)>(
        devicePtr->getSymbol("lib_on_unload"));
    if (fn) {
      fn(data);
    }
  });

  devicePtr->registerReloadCallback("device_reload", [devicePtr](void *data) {
    std::println("[ModuleReloadManager] Device library reloading");

    auto fn = reinterpret_cast<void (*)(void *)>(
        devicePtr->getSymbol("lib_on_reload"));
    if (fn) {
      fn(data);
    }
  });

  devicePtr->registerDestroyCallback("device_cleanup", [devicePtr](void *data) {
    std::println("[ModuleReloadManager] Device library cleanup");

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

void ModuleReloadManager::setupWindowCallbacks() {
  HotReload *windowPtr = window_.get();

  windowPtr->registerLoadCallback("window_load", [windowPtr](void *data) {
    std::println("[ModuleReloadManager] Window library loaded");

    auto fn =
        reinterpret_cast<void (*)(void *)>(windowPtr->getSymbol("lib_on_load"));
    if (fn) {
      fn(data);
    }
  });

  windowPtr->registerUnloadCallback("window_unload", [windowPtr](void *data) {
    std::println("[ModuleReloadManager] Window library unloading");

    auto fn = reinterpret_cast<void (*)(void *)>(
        windowPtr->getSymbol("lib_on_unload"));
    if (fn) {
      fn(data);
    }
  });

  windowPtr->registerReloadCallback("window_reload", [windowPtr](void *data) {
    std::println("[ModuleReloadManager] Window library reloading");

    auto fn = reinterpret_cast<void (*)(void *)>(
        windowPtr->getSymbol("lib_on_reload"));
    if (fn) {
      fn(data);
    }
  });

  windowPtr->registerDestroyCallback("window_cleanup", [windowPtr](void *data) {
    std::println("[ModuleReloadManager] Window library cleanup");

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

void ModuleReloadManager::cleanupCoreState() {
  destroyCoreState(static_cast<coreState *>(core_->getData()), false);
  core_->setData(nullptr);
}

#endif // ENGINE_DEBUG
