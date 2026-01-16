#include "hot_reload.h"
#include <chrono>
#include <csignal>
#include <fstream>
#include <print>
#include <string>

// ============================================================================
// HotReload Implementation
// ============================================================================

std::set<HotReload *> HotReload::instances;
std::mutex HotReload::registry_mutex;

HotReload::HotReload(const std::string &libName, const std::string &libPath)
: name(libName), path(libPath), lastModTime(0), userData(nullptr) {
  setup_signal_handlers();
  std::lock_guard<std::mutex> lock(registry_mutex);
  instances.insert(this);
}

HotReload::~HotReload() {
  std::lock_guard<std::mutex> lock(registry_mutex);
  instances.erase(this);
  unload();
}

bool HotReload::load() {
  tempPath = makeTempLibPath();

  if (! copy_file()) {
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
    std:: print(stderr, "[HotReload] LoadLibrary failed: {}\n", GetLastError());
    #else
    std::print(stderr, "[HotReload] dlopen failed:  {}\n", dlerror());
    #endif
    return false;
  }

  // Execute load callbacks
  executeCallbacks(loadCallbacks);
  executeSymbolCallbacks(loadSymbols);

  std::print("[HotReload] '{}' loaded successfully.\n", name);
  return true;
}

void HotReload::unload() {
  if (handle) {
    // Execute unload callbacks
    executeCallbacks(unloadCallbacks);
    executeSymbolCallbacks(unloadSymbols);

    #ifdef _WIN32
    FreeLibrary(handle);
    #else
    dlclose(handle);
    #endif
    handle = nullptr;
    std::print("[HotReload] '{}' unloaded.\n", name);
  }

  if (! tempPath.empty()) {
    std::remove(tempPath.c_str());
    tempPath.clear();
  }
}

void *HotReload::getSymbol(const std::string &symbolName) {
  if (!handle)
    return nullptr;
  #ifdef _WIN32
  return reinterpret_cast<void *>(GetProcAddress(handle, symbolName.c_str()));
  #else
  return dlsym(handle, symbolName.c_str());
  #endif
}

bool HotReload::reload() {
  std::print("[HotReload] Reloading '{}'...\n", name);

  // Execute reload callbacks before unloading
  executeCallbacks(reloadCallbacks);
  executeSymbolCallbacks(reloadSymbols);

  unload();
  return load();
}

#ifdef _WIN32
#include <windows.h>
inline std::time_t HotReload::getFileModTime() {
  WIN32_FILE_ATTRIBUTE_DATA fad;
  if (! GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad))
    return 0;
  FILETIME ft = fad.ftLastWriteTime;
  ULARGE_INTEGER ull;
  ull.LowPart = ft.dwLowDateTime;
  ull. HighPart = ft.dwHighDateTime;
  return static_cast<std::time_t>((ull.QuadPart / 10000000ULL) -
  11644473600ULL);
}
#else
#include <sys/stat.h>
inline std::time_t HotReload::getFileModTime() {
  struct stat result;
  if (stat(path.c_str(), &result) == 0)
    return result.st_mtime;
  return 0;
}
#endif

bool HotReload:: checkAndReloadIfNeeded() {
  std::time_t modTime = getFileModTime();
  if (modTime > 0 && modTime != lastModTime) {
    lastModTime = modTime;
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
  std::string stem = (dot != std::string:: npos) ? path.substr(0, dot) : path;
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
}

void HotReload::cleanup_all(int /*signum*/) {
  std::lock_guard<std::mutex> lock(registry_mutex);
  for (auto *inst : instances) {
    if (inst) {
      inst->unload();
      std::print(stderr, "[HotReload] Cleaned up '{}' on signal.\n",
                 inst->name);
    }
  }
  std::_Exit(1);
}

// ============================================================================
// Lifecycle Callback Management
// ============================================================================

void HotReload::registerLoadCallback(const std::string &name,
                                     LifecycleCallback callback) {
  loadCallbacks.push_back({name, std::move(callback)});
                                     }

                                     void HotReload::registerUnloadCallback(const std::string &name,
                                                                            LifecycleCallback callback) {
                                       unloadCallbacks. push_back({name, std:: move(callback)});
                                                                            }

                                                                            void HotReload::registerReloadCallback(const std::string &name,
                                                                                                                   LifecycleCallback callback) {
                                                                              reloadCallbacks. push_back({name, std:: move(callback)});
                                                                                                                   }

                                                                                                                   void HotReload::registerLoadSymbol(const std::string &symbolName) {
                                                                                                                     loadSymbols.push_back({symbolName, symbolName});
                                                                                                                   }

                                                                                                                   void HotReload::registerUnloadSymbol(const std::string &symbolName) {
                                                                                                                     unloadSymbols.push_back({symbolName, symbolName});
                                                                                                                   }

                                                                                                                   void HotReload::registerReloadSymbol(const std:: string &symbolName) {
                                                                                                                     reloadSymbols.push_back({symbolName, symbolName});
                                                                                                                   }

                                                                                                                   void HotReload::executeCallbacks(std::vector<CallbackEntry> &callbacks) {
                                                                                                                     for (auto &entry : callbacks) {
                                                                                                                       try {
                                                                                                                         entry.callback(userData);
                                                                                                                         std::print("[HotReload] Executed callback: '{}'\n", entry.name);
                                                                                                                       } catch (const std:: exception &e) {
                                                                                                                         std::print(stderr, "[HotReload] Callback '{}' threw exception: {}\n",
                                                                                                                                    entry. name, e.what());
                                                                                                                       }
                                                                                                                     }
                                                                                                                   }

                                                                                                                   void HotReload::executeSymbolCallbacks(
                                                                                                                     std::vector<SymbolCallbackEntry> &entries) {
                                                                                                                     for (auto &entry :  entries) {
                                                                                                                       auto *func = reinterpret_cast<SymbolCallback>(getSymbol(entry.symbolName));
                                                                                                                       if (func) {
                                                                                                                         try {
                                                                                                                           func(userData);
                                                                                                                           std::print("[HotReload] Executed symbol callback: '{}'\n", entry.name);
                                                                                                                         } catch (const std::exception &e) {
                                                                                                                           std::print(stderr,
                                                                                                                                      "[HotReload] Symbol callback '{}' threw exception: {}\n",
                                                                                                                                      entry.name, e.what());
                                                                                                                         }
                                                                                                                       } else {
                                                                                                                         std::print(stderr, "[HotReload] Symbol '{}' not found in library.\n",
                                                                                                                                    entry.symbolName);
                                                                                                                       }
                                                                                                                     }
                                                                                                                     }

