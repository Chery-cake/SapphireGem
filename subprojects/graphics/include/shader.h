#pragma once

#include "logical_device.h"
#include "vulkan/vulkan.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace render {

// Shader class handles all shader types supported by Vulkan and Slang
class Shader {
public:
  // Shader type enumeration covering all Vulkan shader stages
  enum class ShaderType {
    VERTEX,
    FRAGMENT,
    GEOMETRY,
    TESSELLATION_CONTROL,
    TESSELLATION_EVALUATION,
    COMPUTE,
    // Extended shader types (if supported by device)
    MESH,
    TASK,
    RAY_GEN,
    ANY_HIT,
    CLOSEST_HIT,
    MISS,
    INTERSECTION,
    CALLABLE
  };

  // Information about a specific shader stage
  struct ShaderStageInfo {
    ShaderType type;
    std::string filePath; // Path to the shader source file
    std::string entryPoint = "main"; // Entry point function name
    std::vector<char> spirvCode; // Compiled SPIR-V code
    bool isCompiled = false;
  };

  struct ShaderCreateInfo {
    std::string identifier;
    std::vector<ShaderStageInfo> stages;
  };

  // Per-device shader resources
  struct DeviceShaderResources {
    std::unordered_map<ShaderType, vk::raii::ShaderModule> shaderModules;
  };

private:
  mutable std::mutex shaderMutex;

  std::string identifier;
  std::vector<ShaderStageInfo> stages;
  std::vector<device::LogicalDevice *> logicalDevices;
  std::vector<std::unique_ptr<DeviceShaderResources>> deviceResources;

  // Helper methods
  bool compile_shader_from_file(ShaderStageInfo &stageInfo);
  bool create_shader_module(device::LogicalDevice *device,
                           const std::vector<char> &code,
                           vk::raii::ShaderModule &shaderModule);
  vk::ShaderStageFlagBits get_vulkan_shader_stage(ShaderType type) const;

public:
  Shader(const std::vector<device::LogicalDevice *> &devices,
         const ShaderCreateInfo &createInfo);
  ~Shader();

  // Compile all shader stages
  bool compile();

  // Compile a specific stage
  bool compile_stage(ShaderType type);

  // Initialize shader modules for all devices
  bool initialize();

  // Get shader module for a specific device and stage
  vk::raii::ShaderModule *get_shader_module(ShaderType type,
                                            uint32_t deviceIndex = 0);

  // Get shader stage create info for pipeline creation
  std::vector<vk::PipelineShaderStageCreateInfo>
  get_pipeline_stage_infos(uint32_t deviceIndex = 0) const;

  // Check if shader has a specific stage
  bool has_stage(ShaderType type) const;

  // Get all shader stages
  const std::vector<ShaderStageInfo> &get_stages() const;

  // Getters
  const std::string &get_identifier() const;

  // Static helper to convert shader type to string
  static std::string shader_type_to_string(ShaderType type);
};

} // namespace render
