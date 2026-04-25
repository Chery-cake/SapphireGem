#ifndef HOT_RELOAD_H_
#define HOT_RELOAD_H_
#ifdef ENGINE_DEBUG

#include "signal.hpp"
#include <ctime>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

class HotReload {
public:
  // Lifecycle callback types
  using LifecycleCallback = std::function<void(void *, std::mutex &)>;

  HotReload(const std::string &libName, const std::string &libPath);
  virtual ~HotReload();

  bool load();
  void unload();
  void *getSymbol(const std::string &symbolName) const;

  bool reload();
  void destroy();
  std::time_t getFileModTime();
  bool checkAndReloadIfNeeded();

  // Module dependency management
  void addDependent(HotReload *dependent);
  void reloadDependents();

  // Accessors
  const std::string &getName() const { return name; }

  // Data pointer (passed to all callbacks)
  void setData(void *newData) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    data = newData;
  }
  void *getData() const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    return data;
  }

  // Lifecycle signals
  using SignalCall = void(void *, std::mutex &);
  core::signal::Signal<SignalCall> loadSignal;
  core::signal::Signal<SignalCall> unloadSignal;
  core::signal::Signal<SignalCall> reloadSignal;
  core::signal::Signal<SignalCall> destroySignal;

  static core::signal::Signal<void(const std::string &)> fileChanged;

private:
  bool copy_file();
  std::string makeTempLibPath();

  std::string name;
  std::string path;
  std::string tempPath;
  std::time_t lastModTime;
  void *data;

  mutable std::mutex dataMutex_;

  // Module dependency tracking
  std::vector<HotReload *> dependents_;

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

#endif // ENGINE_DEBUG
#endif // HOT_RELOAD_H_
