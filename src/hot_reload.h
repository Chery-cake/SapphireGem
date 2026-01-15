#ifndef HOT_RELOAD_H_
#define HOT_RELOAD_H_

#include "bump_allocator.h"
#include <ctime>
#include <mutex>
#include <set>
#include <string>
#include <functional>
#include <vector>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

class HotReload {
public:
  // Lifecycle callback types
  using LifecycleCallback = std::function<void(void *)>;
  using SymbolCallback = void (*)(void *); // Function pointer from library

  HotReload(const std::string &libName, const std::string &libPath,
            size_t allocatorSize = 1024 * 1024); // 1MB default
  virtual ~HotReload();

  bool load();
  void unload();
  void *getSymbol(const std::string &symbolName);

  bool reload();
  inline std::time_t getFileModTime();
  bool checkAndReloadIfNeeded();

  // Lifecycle callback registration
  void registerLoadCallback(const std::string &name, LifecycleCallback callback);
  void registerUnloadCallback(const std:: string &name,
                              LifecycleCallback callback);
  void registerReloadCallback(const std::string &name,
                              LifecycleCallback callback);

  // Register callbacks from library symbols
  void registerLoadSymbol(const std::string &symbolName);
  void registerUnloadSymbol(const std::string &symbolName);
  void registerReloadSymbol(const std::string &symbolName);

  // Memory management
  BumpAllocator &getAllocator() { return *allocator; }
  void *allocate(size_t size, size_t alignment = alignof(std:: max_align_t));
  void resetAllocator();

  // User data pointer (passed to all callbacks)
  void setUserData(void *data) { userData = data; }
  void *getUserData() const { return userData; }

private:
  struct CallbackEntry {
    std:: string name;
    LifecycleCallback callback;
  };

  struct SymbolCallbackEntry {
    std::string name;
    std::string symbolName;
  };

  bool copy_file();
  std::string makeTempLibPath();
  void executeCallbacks(std::vector<CallbackEntry> &callbacks);
  void executeSymbolCallbacks(std::vector<SymbolCallbackEntry> &entries);

  std::string name;
  std::string path;
  std::string tempPath;
  std::time_t lastModTime;
  void *userData;

  // Lifecycle callbacks
  std::vector<CallbackEntry> loadCallbacks;
  std::vector<CallbackEntry> unloadCallbacks;
  std::vector<CallbackEntry> reloadCallbacks;

  // Symbol-based callbacks
  std::vector<SymbolCallbackEntry> loadSymbols;
  std::vector<SymbolCallbackEntry> unloadSymbols;
  std::vector<SymbolCallbackEntry> reloadSymbols;

  // Memory management
  std::unique_ptr<BumpAllocator> allocator;

  // Instance tracking
  static std::set<HotReload *> instances;
  static std::mutex registry_mutex;
  static void setup_signal_handlers();
  static void cleanup_all(int signum);

  #ifdef _WIN32
  HMODULE handle = nullptr;
  #else
  void *handle = nullptr;
  #endif
};

#endif // HOT_RELOAD_H_
