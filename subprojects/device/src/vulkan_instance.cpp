#include "vulkan_instance.h"
#include <algorithm>
#include <cstring>
#include <iostream>

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
    : dynamicLoader_(std::move(other.dynamicLoader_))
    , instance_(other.instance_)
    , debugMessenger_(other.debugMessenger_)
    , initialized_(other.initialized_)
    , validationEnabled_(other.validationEnabled_) {
    other.instance_ = nullptr;
    other.debugMessenger_ = nullptr;
    other.initialized_ = false;
}

VulkanInstance& VulkanInstance::operator=(VulkanInstance&& other) noexcept {
    if (this != &other) {
        shutdown();
        dynamicLoader_ = std::move(other.dynamicLoader_);
        instance_ = other.instance_;
        debugMessenger_ = other.debugMessenger_;
        initialized_ = other.initialized_;
        validationEnabled_ = other.validationEnabled_;
        other.instance_ = nullptr;
        other.debugMessenger_ = nullptr;
        other.initialized_ = false;
    }
    return *this;
}

bool VulkanInstance::initialize(const VulkanInstanceConfig& config) {
    if (initialized_) {
        std::cerr << "[VulkanInstance] Already initialized" << std::endl;
        return false;
    }

    // Initialize the dynamic dispatch loader
    auto vkGetInstanceProcAddr = dynamicLoader_.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    if (!vkGetInstanceProcAddr) {
        std::cerr << "[VulkanInstance] Failed to load vkGetInstanceProcAddr" << std::endl;
        return false;
    }
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

    // Check validation layer support
    validationEnabled_ = config.enableValidation;
    std::vector<const char*> enabledLayers;
    if (validationEnabled_) {
        auto unsupported = checkLayerSupport(config.validationLayers);
        if (!unsupported.empty()) {
            std::cerr << "[VulkanInstance] Unsupported validation layers:" << std::endl;
            for (const auto& layer : unsupported) {
                std::cerr << "  - " << layer << std::endl;
            }
            validationEnabled_ = false;
        } else {
            for (const auto& layer : config.validationLayers) {
                enabledLayers.push_back(layer.c_str());
            }
        }
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

    // Add debug extension if validation is enabled
    if (validationEnabled_) {
        enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
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

    // Add debug messenger create info if validation enabled
    vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (validationEnabled_) {
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
        instance_ = vk::createInstance(createInfo);
    } catch (const vk::SystemError& e) {
        std::cerr << "[VulkanInstance] Failed to create instance: " << e.what() << std::endl;
        return false;
    }

    // Initialize dispatch loader with instance
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance_);

    // Setup debug messenger
    if (validationEnabled_) {
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

    destroyDebugMessenger();

    if (instance_) {
        instance_.destroy();
        instance_ = nullptr;
    }

    initialized_ = false;
    validationEnabled_ = false;
    std::cout << "[VulkanInstance] Shutdown complete" << std::endl;
}

bool VulkanInstance::setupDebugMessenger() {
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
        debugMessenger_ = instance_.createDebugUtilsMessengerEXT(createInfo);
        return true;
    } catch (const vk::SystemError& e) {
        std::cerr << "[VulkanInstance] Failed to create debug messenger: " << e.what() << std::endl;
        return false;
    }
}

void VulkanInstance::destroyDebugMessenger() {
    if (debugMessenger_ && instance_) {
        instance_.destroyDebugUtilsMessengerEXT(debugMessenger_);
        debugMessenger_ = nullptr;
    }
}

std::vector<std::string> VulkanInstance::getAvailableExtensions() {
    auto extensions = vk::enumerateInstanceExtensionProperties();
    std::vector<std::string> result;
    result.reserve(extensions.size());
    for (const auto& ext : extensions) {
        result.emplace_back(ext.extensionName.data());
    }
    return result;
}

std::vector<std::string> VulkanInstance::getAvailableLayers() {
    auto layers = vk::enumerateInstanceLayerProperties();
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
