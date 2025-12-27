#include "shader.h"
#include "slang_wasm_compiler.h"
#include <fstream>
#include <print>

render::Shader::Shader(const std::vector<device::LogicalDevice *> &devices,
                       const ShaderCreateInfo &createInfo)
    : identifier(createInfo.identifier), stages(createInfo.stages),
      logicalDevices(devices) {

  deviceResources.reserve(logicalDevices.size());
  for (size_t i = 0; i < logicalDevices.size(); ++i) {
    deviceResources.push_back(std::make_unique<DeviceShaderResources>());
  }

  std::print("Shader - {} - created with {} stages\n", identifier,
             stages.size());
}

render::Shader::~Shader() {
  std::lock_guard lock(shaderMutex);

  for (auto &resources : deviceResources) {
    resources->shaderModules.clear();
  }
  deviceResources.clear();

  std::print("Shader - {} - destructor executed\n", identifier);
}

bool render::Shader::compile_shader_from_file(ShaderStageInfo &stageInfo) {
  if (stageInfo.filePath.empty()) {
    std::print(stderr, "Shader - {} - no file path for stage {}\n", identifier,
               shader_type_to_string(stageInfo.type));
    return false;
  }

  // Get the appropriate entry point based on stage type
  std::string entryPoint;
  switch (stageInfo.type) {
  case ShaderType::VERTEX:
    entryPoint = "vertMain";
    break;
  case ShaderType::FRAGMENT:
    entryPoint = "fragMain";
    break;
  case ShaderType::GEOMETRY:
    entryPoint = "geomMain";
    break;
  case ShaderType::COMPUTE:
    entryPoint = "computeMain";
    break;
  case ShaderType::TESSELLATION_CONTROL:
    entryPoint = "tessControlMain";
    break;
  case ShaderType::TESSELLATION_EVALUATION:
    entryPoint = "tessEvalMain";
    break;
  case ShaderType::MESH:
    entryPoint = "meshMain";
    break;
  case ShaderType::TASK:
    entryPoint = "taskMain";
    break;
  default:
    entryPoint = stageInfo.entryPoint;
    break;
  }

  // Compile using Slang WASM compiler
  thread_local wasm::SlangWasmCompiler compiler;

  std::filesystem::path inputPath(stageInfo.filePath);
  std::filesystem::path outputPath = inputPath;
  outputPath.replace_extension(".spv");

  // Compile the shader to a temporary SPIR-V file
  if (!compiler.compileShaderToSpirv(inputPath, outputPath, {entryPoint})) {
    std::print(stderr,
               "Shader - {} - failed to compile {} from {} (entry: {}): {}\n",
               identifier, shader_type_to_string(stageInfo.type),
               stageInfo.filePath, entryPoint, compiler.getLastError());
    return false;
  }

  // Read the compiled SPIR-V file
  std::ifstream file(outputPath, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    std::print(stderr, "Shader - {} - failed to open compiled SPIR-V file {}\n",
               identifier, outputPath.string());
    return false;
  }

  size_t fileSize = file.tellg();
  stageInfo.spirvCode.resize(fileSize);
  file.seekg(0);
  file.read(stageInfo.spirvCode.data(), fileSize);
  file.close();

  stageInfo.entryPoint = entryPoint;
  stageInfo.isCompiled = true;

  std::print("Shader - {} - compiled {} from {}\n", identifier,
             shader_type_to_string(stageInfo.type), stageInfo.filePath);

  return true;
}

bool render::Shader::create_shader_module(
    device::LogicalDevice *device, const std::vector<char> &code,
    vk::raii::ShaderModule &shaderModule) {
  try {
    vk::ShaderModuleCreateInfo shaderInfo{
        .codeSize = code.size(),
        .pCode = reinterpret_cast<const uint32_t *>(code.data())};
    shaderModule = device->get_device().createShaderModule(shaderInfo);
    return true;
  } catch (const std::exception &e) {
    std::print(stderr, "Shader - {} - failed to create shader module: {}\n",
               identifier, e.what());
    return false;
  }
}

vk::ShaderStageFlagBits
render::Shader::get_vulkan_shader_stage(ShaderType type) const {
  switch (type) {
  case ShaderType::VERTEX:
    return vk::ShaderStageFlagBits::eVertex;
  case ShaderType::FRAGMENT:
    return vk::ShaderStageFlagBits::eFragment;
  case ShaderType::GEOMETRY:
    return vk::ShaderStageFlagBits::eGeometry;
  case ShaderType::TESSELLATION_CONTROL:
    return vk::ShaderStageFlagBits::eTessellationControl;
  case ShaderType::TESSELLATION_EVALUATION:
    return vk::ShaderStageFlagBits::eTessellationEvaluation;
  case ShaderType::COMPUTE:
    return vk::ShaderStageFlagBits::eCompute;
  case ShaderType::MESH:
    return vk::ShaderStageFlagBits::eMeshEXT;
  case ShaderType::TASK:
    return vk::ShaderStageFlagBits::eTaskEXT;
  case ShaderType::RAY_GEN:
    return vk::ShaderStageFlagBits::eRaygenKHR;
  case ShaderType::ANY_HIT:
    return vk::ShaderStageFlagBits::eAnyHitKHR;
  case ShaderType::CLOSEST_HIT:
    return vk::ShaderStageFlagBits::eClosestHitKHR;
  case ShaderType::MISS:
    return vk::ShaderStageFlagBits::eMissKHR;
  case ShaderType::INTERSECTION:
    return vk::ShaderStageFlagBits::eIntersectionKHR;
  case ShaderType::CALLABLE:
    return vk::ShaderStageFlagBits::eCallableKHR;
  default:
    return vk::ShaderStageFlagBits::eVertex;
  }
}

bool render::Shader::compile() {
  std::lock_guard lock(shaderMutex);

  for (auto &stage : stages) {
    if (!stage.isCompiled) {
      if (!compile_shader_from_file(stage)) {
        return false;
      }
    }
  }

  std::print("Shader - {} - all stages compiled successfully\n", identifier);
  return true;
}

bool render::Shader::compile_stage(ShaderType type) {
  std::lock_guard lock(shaderMutex);

  for (auto &stage : stages) {
    if (stage.type == type && !stage.isCompiled) {
      return compile_shader_from_file(stage);
    }
  }

  return false;
}

bool render::Shader::initialize() {
  std::lock_guard lock(shaderMutex);

  // Make sure all stages are compiled
  for (const auto &stage : stages) {
    if (!stage.isCompiled) {
      std::print(stderr,
                 "Shader - {} - cannot initialize: stage {} not compiled\n",
                 identifier, shader_type_to_string(stage.type));
      return false;
    }
  }

  // Create shader modules for each device
  for (size_t deviceIdx = 0; deviceIdx < logicalDevices.size(); ++deviceIdx) {
    auto *device = logicalDevices[deviceIdx];
    auto &resources = deviceResources[deviceIdx];

    for (const auto &stage : stages) {
      vk::raii::ShaderModule shaderModule{nullptr};
      if (!create_shader_module(device, stage.spirvCode, shaderModule)) {
        std::print(stderr,
                   "Shader - {} - failed to create shader module for "
                   "stage {}\n",
                   identifier, shader_type_to_string(stage.type));
        return false;
      }
      resources->shaderModules[stage.type] =
          std::make_unique<vk::raii::ShaderModule>(std::move(shaderModule));
    }
  }

  std::print("Shader - {} - initialized for {} devices\n", identifier,
             logicalDevices.size());
  return true;
}

vk::raii::ShaderModule *
render::Shader::get_shader_module(ShaderType type, uint32_t deviceIndex) {
  std::lock_guard lock(shaderMutex);

  if (deviceIndex >= deviceResources.size()) {
    return nullptr;
  }

  auto &resources = deviceResources[deviceIndex];
  auto it = resources->shaderModules.find(type);
  if (it != resources->shaderModules.end()) {
    return it->second.get();
  }

  return nullptr;
}

std::vector<vk::PipelineShaderStageCreateInfo>
render::Shader::get_pipeline_stage_infos(uint32_t deviceIndex) const {
  std::lock_guard lock(shaderMutex);

  std::vector<vk::PipelineShaderStageCreateInfo> stageInfos;

  if (deviceIndex >= deviceResources.size()) {
    return stageInfos;
  }

  auto &resources = deviceResources[deviceIndex];

  for (const auto &stage : stages) {
    auto it = resources->shaderModules.find(stage.type);
    if (it != resources->shaderModules.end()) {
      vk::PipelineShaderStageCreateInfo stageInfo{
          .stage = get_vulkan_shader_stage(stage.type),
          .module = **it->second,
          .pName = stage.entryPoint.c_str()};
      stageInfos.push_back(stageInfo);
    }
  }

  return stageInfos;
}

bool render::Shader::has_stage(ShaderType type) const {
  std::lock_guard lock(shaderMutex);

  for (const auto &stage : stages) {
    if (stage.type == type) {
      return true;
    }
  }
  return false;
}

const std::vector<render::Shader::ShaderStageInfo> &
render::Shader::get_stages() const {
  return stages;
}

const std::string &render::Shader::get_identifier() const { return identifier; }

std::string render::Shader::shader_type_to_string(ShaderType type) {
  switch (type) {
  case ShaderType::VERTEX:
    return "Vertex";
  case ShaderType::FRAGMENT:
    return "Fragment";
  case ShaderType::GEOMETRY:
    return "Geometry";
  case ShaderType::TESSELLATION_CONTROL:
    return "Tessellation Control";
  case ShaderType::TESSELLATION_EVALUATION:
    return "Tessellation Evaluation";
  case ShaderType::COMPUTE:
    return "Compute";
  case ShaderType::MESH:
    return "Mesh";
  case ShaderType::TASK:
    return "Task";
  case ShaderType::RAY_GEN:
    return "Ray Generation";
  case ShaderType::ANY_HIT:
    return "Any Hit";
  case ShaderType::CLOSEST_HIT:
    return "Closest Hit";
  case ShaderType::MISS:
    return "Miss";
  case ShaderType::INTERSECTION:
    return "Intersection";
  case ShaderType::CALLABLE:
    return "Callable";
  default:
    return "Unknown";
  }
}
