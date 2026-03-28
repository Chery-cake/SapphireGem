#include "config.h"
#include "config_threads.h"
#include "config_vulkan.h"
#include "vulkan/vulkan.hpp"
#include <algorithm>
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
      pendingChanges_, immediateMode_, callbacks_, configMutex_);
  threadsConfig_ = std::make_unique<ThreadsConfig>(
      pendingChanges_, immediateMode_, callbacks_, configMutex_);
}

Config::~Config() { shutdown(); }

void Config::shutdown() {
  std::lock_guard<std::mutex> lock(configMutex_);
  vulkanConfig_.reset();
  threadsConfig_.reset();
  callbacks_.clear();
  pendingChanges_ = ConfigSection::None;
}

void Config::resetToDefaults() {
  std::vector<CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configMutex_);

    vulkanConfig_->resetToDefaults();
    threadsConfig_->resetToDefaults();

    pendingChanges_ = ConfigSection::All;

    if (immediateMode_) {
      std::ranges::for_each(callbacks_.begin(), callbacks_.end(),
                            [&](const auto &entry) {
                              if (hasFlag(pendingChanges_, entry.sections)) {
                                callbacksToNotify.push_back(entry);
                              }
                            });

      pendingChanges_ = ConfigSection::None;
    }
  }

  // Notify callbacks outside lock
  std::ranges::for_each(callbacksToNotify.begin(), callbacksToNotify.end(),
                        [](const auto &entry) { entry.callback(); });
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
  std::vector<CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configMutex_);
    if (*vulkanConfig_ != config) {
      *vulkanConfig_ = config;
      changed = true;

      if (immediateMode_) {

        std::ranges::for_each(
            callbacks_.begin(), callbacks_.end(),
            [&callbacksToNotify](const auto &entry) {
              if (hasFlag(entry.sections, ConfigSection::Vulkan)) {
                callbacksToNotify.push_back(entry);
              }
            });
      } else {
        pendingChanges_ = pendingChanges_ | ConfigSection::Vulkan;
      }
    }
  }

  // Notify callbacks outside lock
  if (changed && immediateMode_) {
    std::ranges::for_each(callbacksToNotify.begin(), callbacksToNotify.end(),
                          [](const auto &entry) { entry.callback(); });
  }
}

const VulkanConfig &Config::getVulkanConfig() const {
  std::lock_guard<std::mutex> lock(configMutex_);
  return *vulkanConfig_;
}

// ========== Change Callbacks ==========

bool Config::registerChangeCallback(const std::string &name,
                                    ConfigSection sections,
                                    ConfigChangeCallback callback) {
  std::lock_guard<std::mutex> lock(configMutex_);

  // Check if callback with this name already exists
  if (std::ranges::any_of(
          callbacks_.begin(), callbacks_.end(),
          [&name](const auto &entry) { return entry.name == name; })) {
    return false;
  }

  callbacks_.push_back(CallbackEntry(name, sections, std::move(callback)));
  return true;
}

bool Config::unregisterChangeCallback(const std::string &name) {
  std::lock_guard<std::mutex> lock(configMutex_);

  auto it = std::ranges::find_if(
      callbacks_.begin(), callbacks_.end(),
      [&name](const CallbackEntry &entry) { return entry.name == name; });

  if (it != callbacks_.end()) {
    callbacks_.erase(it);
    return true;
  }

  return false;
}

std::vector<std::string> Config::getCallbackNames() const {
  std::lock_guard<std::mutex> lock(configMutex_);

  std::vector<std::string> names;
  names.reserve(callbacks_.size());
  std::ranges::for_each(
      callbacks_.begin(), callbacks_.end(),
      [&names](const auto &entry) { names.push_back(entry.name); });
  return names;
}

void Config::applyPendingChanges() {
  ConfigSection changes;
  std::vector<CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configMutex_);
    changes = pendingChanges_;
    pendingChanges_ = ConfigSection::None;

    if (changes != ConfigSection::None) {
      std::ranges::for_each(callbacks_.begin(), callbacks_.end(),
                            [&changes, &callbacksToNotify](const auto &entry) {
                              if (hasFlag(changes, entry.sections)) {
                                callbacksToNotify.push_back(entry);
                              }
                            });
    }
  }

  // Notify callbacks outside lock
  std::ranges::for_each(callbacksToNotify.begin(), callbacksToNotify.end(),
                        [](const auto &entry) { entry.callback(); });
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
  std::vector<CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configMutex_);
    std::ranges::for_each(
        callbacks_.begin(), callbacks_.end(),
        [&changedSections, &callbacksToNotify](const auto &entry) {
          if (hasFlag(changedSections, entry.sections)) {
            callbacksToNotify.push_back(entry);
          }
        });
  }

  // Notify callbacks outside lock
  std::ranges::for_each(callbacksToNotify.begin(), callbacksToNotify.end(),
                        [](const auto &entry) { entry.callback(); });
}

} // namespace core
