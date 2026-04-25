#include <exception>
#include <filesystem>
#ifdef ENGINE_DEBUG

#include "hot_reload.h"
#include <chrono>
#include <csignal>
#include <fstream>
#include <mutex>
#include <print>
#include <string>

// ============================================================================
// HotReload Implementation
// ============================================================================

std::set<HotReload *> HotReload::instances;
std::mutex HotReload::registry_mutex;
core::signal::Signal<void(const std::string &)> HotReload::fileChanged;

HotReload::HotReload(const std::string &libName, const std::string &libPath)
    : name(libName), path(libPath), lastModTime(0), data(nullptr) {
  setup_signal_handlers();
  std::lock_guard<std::mutex> lock(registry_mutex);
  instances.insert(this);
}

HotReload::~HotReload() {
  std::lock_guard<std::mutex> lock(registry_mutex);
  instances.erase(this);
  destroySignal.emit(data, dataMutex_);
  unload();
}

bool HotReload::load() {
  tempPath = makeTempLibPath();

  if (!copy_file()) {
    std::print(stderr, "[HotReload] Failed to copy library file.\n");
    return false;
  }

#ifdef _WIN32
  handle = LoadLibrary(tempPath.c_str());
#else
  handle = dlopen(tempPath.c_str(), RTLD_NOW);
#endif

  if (!handle) {
#ifdef _WIN32
    std::print(stderr, "[HotReload] LoadLibrary failed: {}\n", GetLastError());
#else
    std::print(stderr, "[HotReload] dlopen failed:  {}\n", dlerror());
#endif
    return false;
  }

  // Execute load signal
  loadSignal.emit(data, dataMutex_);

  std::print("[HotReload] '{}' loaded successfully.\n", name);
  return true;
}

void HotReload::unload() {
  if (handle) {
    // Execute unload signal
    unloadSignal.emit(data, dataMutex_);

#ifdef _WIN32
    FreeLibrary(handle);
#else
    dlclose(handle);
#endif
    handle = nullptr;
    std::print("[HotReload] '{}' unloaded.\n", name);
  }

  if (!tempPath.empty()) {
    std::remove(tempPath.c_str());
    tempPath.clear();
  }
}

void *HotReload::getSymbol(const std::string &symbolName) const {
  if (!handle)
    return nullptr;
#ifdef _WIN32
  return reinterpret_cast<void *>(GetProcAddress(handle, symbolName.c_str()));
#else
  return dlsym(handle, symbolName.c_str());
#endif
}

bool HotReload::reload() {
  std::println("[HotReload] Reloading '{}'...", name);

  // Execute reload signal before unloading to save state
  reloadSignal.emit(data, dataMutex_);

  unload();
  bool success = load();

  // Cascade reload to dependent modules
  if (success) {
    reloadDependents();
  }

  return success;
}

void HotReload::destroy() {
  std::println("[HotReload] Destroying '{}'...", name);
  destroySignal.emit(data, dataMutex_);
}

std::time_t HotReload::getFileModTime() {
  std::filesystem::path p(path);

  try {
    auto fileTime = std::filesystem::last_write_time(p);
    auto timeDiff =
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            fileTime - std::filesystem::file_time_type::clock::now() +
            std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(timeDiff);
  } catch (std::exception &e) {
    std::println("[HotReload] Couldn't get the write time from library {} with "
                 "error: {}",
                 name, e.what());
    return 0;
  }
}

bool HotReload::checkAndReloadIfNeeded() {
  std::time_t modTime = getFileModTime();
  if (modTime > 0 && modTime != lastModTime) {
    lastModTime = modTime;
    fileChanged.emit(name);
    return reload();
  }
  return false;
}

bool HotReload::copy_file() {
  std::ifstream src(path, std::ios::binary);
  if (!src) {
    std::print(stderr, "[HotReload] Cannot open source file: {}\n", path);
    return false;
  }

  std::ofstream dst(tempPath, std::ios::binary);
  if (!dst) {
    std::print(stderr, "[HotReload] Cannot create temp file: {}\n", tempPath);
    return false;
  }

  dst << src.rdbuf();
  return src && dst;
}

std::string HotReload::makeTempLibPath() {
  auto now = std::chrono::system_clock::now().time_since_epoch().count();

  auto dot = path.rfind('.');
  std::string ext = (dot != std::string::npos) ? path.substr(dot) : "";
  std::string stem = (dot != std::string::npos) ? path.substr(0, dot) : path;
  return stem + "_tmp_" + std::to_string(now) + ext;
}

void HotReload::setup_signal_handlers() {
  static bool already_set = false;
  if (already_set)
    return;
  already_set = true;

  auto fn = cleanup_all;
  std::signal(SIGINT, fn);
  std::signal(SIGTERM, fn);
  std::signal(SIGABRT, fn);
  std::signal(SIGSEGV, fn);
}

void HotReload::cleanup_all(int signum) {
  std::lock_guard<std::mutex> lock(registry_mutex);
  for (auto *inst : instances) {
    if (inst) {
      inst->unload();
      std::print(stderr, "[HotReload] Cleaned up '{}' on signal {}.\n",
                 inst->name, signum);
    }
  }
  std::_Exit(1);
}

// ============================================================================
// Module Dependency Management
// ============================================================================

void HotReload::addDependent(HotReload *dependent) {
  if (dependent) {
    dependents_.push_back(dependent);
    std::print("[HotReload] '{}' added dependent '{}'\n", name,
               dependent->getName());
  }
}

void HotReload::reloadDependents() {
  for (auto *dep : dependents_) {
    if (dep) {
      std::print("[HotReload] Cascade reloading dependent '{}' from '{}'\n",
                 dep->getName(), name);
      dep->reload();
    }
  }
}

#endif // ENGINE_DEBUG
