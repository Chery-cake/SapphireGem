#include "vulkan_instance.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <unordered_set>

namespace device {

// Debug callback for validation layers
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    (void)pUserData;
    (void)messageType;

    const char* severity = "UNKNOWN";
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
        severity = "VERBOSE";
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        severity = "INFO";
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        severity = "WARNING";
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        severity = "ERROR";
    }

    std::cerr << "[Vulkan " << severity << "] " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

VulkanInstance::VulkanInstance() = default;

VulkanInstance::~VulkanInstance() {
    shutdown();
}

VulkanInstance::VulkanInstance(VulkanInstance&& other) noexcept
    : context_(std::move(other.context_))
    , instance_(std::move(other.instance_))
    , debugMessenger_(std::move(other.debugMessenger_))
    , initialized_(other.initialized_)
    , hasLayers_(other.hasLayers_) {
    other.initialized_ = false;
    other.hasLayers_ = false;
}

VulkanInstance& VulkanInstance::operator=(VulkanInstance&& other) noexcept {
    if (this != &other) {
        shutdown();
        context_ = std::move(other.context_);
        instance_ = std::move(other.instance_);
        debugMessenger_ = std::move(other.debugMessenger_);
        initialized_ = other.initialized_;
        hasLayers_ = other.hasLayers_;
        other.initialized_ = false;
        other.hasLayers_ = false;
    }
    return *this;
}

bool VulkanInstance::initialize(const VulkanInstanceConfig& config) {
    if (initialized_) {
        std::cerr << "[VulkanInstance] Already initialized" << std::endl;
        return false;
    }

    try {
        // Create RAII context (loads Vulkan library)
        context_ = std::make_unique<vk::raii::Context>();
    } catch (const vk::SystemError& e) {
        std::cerr << "[VulkanInstance] Failed to create context: " << e.what() << std::endl;
        return false;
    }

    // Check instance layer support and add supported layers
    std::vector<const char*> enabledLayers;
    if (!config.instanceLayers.empty()) {
        auto unsupportedList = checkLayerSupport(config.instanceLayers);
        std::unordered_set<std::string> unsupported(unsupportedList.begin(), unsupportedList.end());
        if (!unsupported.empty()) {
            std::cerr << "[VulkanInstance] Skipping unsupported instance layers:" << std::endl;
            for (const auto& layer : unsupportedList) {
                std::cerr << "  - " << layer << std::endl;
            }
        }
        for (const auto& layer : config.instanceLayers) {
            if (unsupported.find(layer) == unsupported.end()) {
                enabledLayers.push_back(layer.c_str());
            }
        }
        hasLayers_ = !enabledLayers.empty();
    }

    // Check extension support
    auto unsupportedExtensions = checkExtensionSupport(config.requiredExtensions);
    if (!unsupportedExtensions.empty()) {
        std::cerr << "[VulkanInstance] Unsupported extensions:" << std::endl;
        for (const auto& ext : unsupportedExtensions) {
            std::cerr << "  - " << ext << std::endl;
        }
        return false;
    }

    // Prepare extension list
    std::vector<const char*> enabledExtensions;
    for (const auto& ext : config.requiredExtensions) {
        enabledExtensions.push_back(ext.c_str());
    }

    // Add debug extension if layers are enabled (for debug messenger)
    if (hasLayers_) {
        enabledExtensions.push_back(vk::EXTDebugUtilsExtensionName);
    }

    // Create application info
    vk::ApplicationInfo appInfo{
        config.applicationName.c_str(),
        config.applicationVersion,
        config.engineName.c_str(),
        config.engineVersion,
        config.apiVersion
    };

    // Create instance
    vk::InstanceCreateInfo createInfo{
        {},
        &appInfo,
        enabledLayers,
        enabledExtensions
    };

    // Add debug messenger create info if layers enabled
    vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (hasLayers_) {
        debugCreateInfo.messageSeverity =
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
        debugCreateInfo.messageType =
            vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
            vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
            vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
        debugCreateInfo.pfnUserCallback = debugCallback;

        createInfo.pNext = &debugCreateInfo;
    }

    try {
        instance_ = std::make_unique<vk::raii::Instance>(*context_, createInfo);
    } catch (const vk::SystemError& e) {
        std::cerr << "[VulkanInstance] Failed to create instance: " << e.what() << std::endl;
        return false;
    }

    // Setup debug messenger (RAII handles cleanup automatically)
    if (hasLayers_) {
        if (!setupDebugMessenger()) {
            std::cerr << "[VulkanInstance] Failed to setup debug messenger" << std::endl;
            // Continue anyway, not critical
        }
    }

    initialized_ = true;
    std::cout << "[VulkanInstance] Initialized successfully" << std::endl;
    return true;
}

void VulkanInstance::shutdown() {
    if (!initialized_) {
        return;
    }

    // RAII handles cleanup - destroy in reverse order
    debugMessenger_.reset();
    instance_.reset();
    context_.reset();

    initialized_ = false;
    hasLayers_ = false;
    std::cout << "[VulkanInstance] Shutdown complete" << std::endl;
}

bool VulkanInstance::setupDebugMessenger() {
    if (!instance_) {
        return false;
    }

    vk::DebugUtilsMessengerCreateInfoEXT createInfo{
        {},
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        debugCallback,
        nullptr
    };

    try {
        debugMessenger_ = std::make_unique<vk::raii::DebugUtilsMessengerEXT>(*instance_, createInfo);
        return true;
    } catch (const vk::SystemError& e) {
        std::cerr << "[VulkanInstance] Failed to create debug messenger: " << e.what() << std::endl;
        return false;
    }
}

std::vector<std::string> VulkanInstance::getAvailableExtensions() {
    // Use temporary context for static query
    vk::raii::Context tempContext;
    auto extensions = tempContext.enumerateInstanceExtensionProperties();
    std::vector<std::string> result;
    result.reserve(extensions.size());
    for (const auto& ext : extensions) {
        result.emplace_back(ext.extensionName.data());
    }
    return result;
}

std::vector<std::string> VulkanInstance::getAvailableLayers() {
    // Use temporary context for static query
    vk::raii::Context tempContext;
    auto layers = tempContext.enumerateInstanceLayerProperties();
    std::vector<std::string> result;
    result.reserve(layers.size());
    for (const auto& layer : layers) {
        result.emplace_back(layer.layerName.data());
    }
    return result;
}

std::vector<std::string> VulkanInstance::checkExtensionSupport(
    const std::vector<std::string>& extensions) {
    auto available = getAvailableExtensions();
    std::vector<std::string> unsupported;

    for (const auto& ext : extensions) {
        if (std::find(available.begin(), available.end(), ext) == available.end()) {
            unsupported.push_back(ext);
        }
    }

    return unsupported;
}

std::vector<std::string> VulkanInstance::checkLayerSupport(
    const std::vector<std::string>& layers) {
    auto available = getAvailableLayers();
    std::vector<std::string> unsupported;

    for (const auto& layer : layers) {
        if (std::find(available.begin(), available.end(), layer) == available.end()) {
            unsupported.push_back(layer);
        }
    }

    return unsupported;
}

} // namespace device
