#include "config.h"
#include "config_threads.h"
#include "config_vulkan.h"
#include "signal_fwd.h"
#include <memory>
#include <mutex>

namespace core {

#ifdef ENGINE_DEBUG
static Config *g_configInstance = nullptr;
static std::mutex g_configMutex;

Config &Config::instance() {
  std::lock_guard<std::mutex> lock(g_configMutex);
  if (!g_configInstance) {
    g_configInstance = new Config();
  }
  return *g_configInstance;
}

void Config::setInstance(Config *inst) {
  // Called by hot reload system when coordinating instance swap.
  // Caller is responsible for managing the old instance's lifetime.
  std::lock_guard<std::mutex> lock(g_configMutex);
  g_configInstance = inst;
}

Config *Config::getInstance() {
  std::lock_guard<std::mutex> lock(g_configMutex);
  return g_configInstance;
}

#else
// In release mode, use classic static local variable singleton
Config &Config::instance() {
  static Config instance;
  return instance;
}
#endif

Config::Config() {
  vulkanConfig_ = std::make_unique<VulkanConfig>(
      pendingChanges_, immediateMode_, vulkanChanged, configMutex_);
  threadsConfig_ = std::make_unique<ThreadsConfig>(
      pendingChanges_, immediateMode_, threadPoolChanged, gpuChanged,
      loopChanged, configMutex_);
}

Config::~Config() { shutdown(); }

void Config::shutdown() {
  std::lock_guard<std::mutex> lock(configMutex_);
  vulkanConfig_.reset();
  threadsConfig_.reset();

  vulkanChanged.clear();
  threadPoolChanged.clear();
  gpuChanged.clear();
  loopChanged.clear();

  pendingChanges_ = ConfigSection::None;
}

void Config::resetToDefaults() {
  {
    std::lock_guard<std::mutex> lock(configMutex_);

    vulkanConfig_->resetToDefaults();
    threadsConfig_->resetToDefaults();

    pendingChanges_ = ConfigSection::All;
  }

  // Notify callbacks outside lock
  if (immediateMode_) {

    notifyCallbacks(ConfigSection::All);

    pendingChanges_ = ConfigSection::None;
  }
}

// ========== Application Configuration ==========

void Config::setApplicationConfig(const ApplicationConfig &config) {
  std::lock_guard<std::mutex> lock(configMutex_);
  if (applicationConfig_ != config) {
    applicationConfig_ = config;
  }
}

const ApplicationConfig &Config::getApplicationConfig() const {
  std::lock_guard<std::mutex> lock(configMutex_);
  return applicationConfig_;
}

// ========== Vulkan Configuration ==========

void Config::setVulkanConfig(const VulkanConfig &config) {
  bool changed = false;

  {
    std::lock_guard<std::mutex> lock(configMutex_);
    if (*vulkanConfig_ != config) {
      *vulkanConfig_ = config;
      changed = true;

      if (!immediateMode_) {
        pendingChanges_ = pendingChanges_ | ConfigSection::Vulkan;
      }
    }
  }

  // Notify callbacks outside lock
  if (changed && immediateMode_) {
    notifyCallbacks(ConfigSection::Vulkan);
  }
}

VulkanConfig &Config::getVulkanConfig() {
  std::lock_guard<std::mutex> lock(configMutex_);
  return *vulkanConfig_;
}

// ========== Threads Configuration ==========

void Config::setThreadsConfig(const ThreadsConfig &config) {
  bool changed = false;
  ConfigSection flags =
      ConfigSection::GPU | ConfigSection::Loop | ConfigSection::ThreadPool;

  {
    std::lock_guard<std::mutex> lock(configMutex_);
    if (*threadsConfig_ != config) {
      *threadsConfig_ = config;
      changed = true;
      if (!immediateMode_) {
        pendingChanges_ = pendingChanges_ | flags;
      }
    }
  }

  // Notify callbacks outside lock
  if (changed && immediateMode_) {
    notifyCallbacks(flags);
  }
}

ThreadsConfig &Config::getThreadsConfig() {
  std::lock_guard<std::mutex> lock(configMutex_);
  return *threadsConfig_;
}

// ========== Change Signals ==========

void Config::applyPendingChanges() {
  ConfigSection changes;

  {
    std::lock_guard<std::mutex> lock(configMutex_);
    changes = pendingChanges_;
    pendingChanges_ = ConfigSection::None;

    notifyCallbacks(changes);
  }
}

void Config::setImmediateMode(bool immediate) {
  std::lock_guard<std::mutex> lock(configMutex_);
  immediateMode_ = immediate;
}

bool Config::isImmediateMode() const {
  std::lock_guard<std::mutex> lock(configMutex_);
  return immediateMode_;
}

void Config::notifyCallbacks(ConfigSection changedSections) {
  if (hasFlag(changedSections, ConfigSection::Vulkan))
    vulkanChanged.emit();
  if (hasFlag(changedSections, ConfigSection::ThreadPool))
    threadPoolChanged.emit();
  if (hasFlag(changedSections, ConfigSection::GPU))
    gpuChanged.emit();
  if (hasFlag(changedSections, ConfigSection::Loop))
    loopChanged.emit();
}

} // namespace core
