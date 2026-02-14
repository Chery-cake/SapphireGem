#ifndef SHADER_MANAGER_H_
#define SHADER_MANAGER_H_

#include "device_export.h"
#include "vulkan/vulkan.hpp"
#include "vulkan_device.h"
#include <cstdint>
#include <future>
#include <string>
#include <unordered_map>

// Forward declare Slang types to avoid including slang.h in header
struct SlangSession;
struct SlangCompileRequest;

namespace device {

/**
 * @brief Shader stage types
 */
enum class DEVICE_API ShaderStage : uint8_t {
  Vertex,
  Fragment,
  Geometry,
  TessellationControl,
  TessellationEvaluation,
  Compute
};

/**
 * @brief Compiled shader module information
 */
struct DEVICE_API CompiledShader {
  vk::ShaderModule module; // TODO change to vk::raii::ShaderModule if possible
  ShaderStage stage;
  std::string entryPoint;
  std::string sourcePath; // Original source path
  std::string sourceHash; // Hash for hot reload detection
  bool isValid = false;

  [[nodiscard]] vk::ShaderStageFlagBits getVkStage() const;
  [[nodiscard]] vk::PipelineShaderStageCreateInfo getStageInfo() const;
};

/**
 * @brief Shader compilation request
 */
struct DEVICE_API ShaderCompileRequest {
  std::string sourcePath;                // Path to .slang file
  std::string entryPoint;                // Entry point function name
  ShaderStage stage;                     // Target shader stage
  std::vector<std::string> defines;      // Preprocessor defines
  std::vector<std::string> includePaths; // Include search paths
};

/**
 * @brief Result of shader compilation
 */
struct DEVICE_API ShaderCompileResult {
  bool success = false;
  std::string errorMessage;
  std::vector<uint32_t> spirvCode; // Compiled SPIR-V bytecode
  std::string sourceHash;
};

/**
 * @brief Manages Slang shader compilation at runtime
 *
 * Features:
 * - Runtime compilation of Slang shaders to SPIR-V
 * - Support for multiple entry points in single .slang file
 * - Caching of compiled shaders
 * - Parallel compilation using thread pools
 * - Hot reload support (comparing source hashes)
 */
class DEVICE_API ShaderManager {
public:
  ShaderManager();
  ~ShaderManager();

  // Disable copy
  ShaderManager(const ShaderManager &) = delete;
  ShaderManager &operator=(const ShaderManager &) = delete;

  /**
   * @brief Initialize the shader manager
   * @param device GPU device to create shader modules for
   * @return true if initialization succeeded
   */
  bool initialize(GPUDevice &device);

  /**
   * @brief Shutdown and cleanup all shaders
   */
  void shutdown();

  [[nodiscard]] bool isInitialized() const { return initialized_; }

  /**
   * @brief Set the base path for shader files
   * @param basePath Base directory for shader lookup
   */
  void setShaderBasePath(const std::string &basePath);

  /**
   * @brief Add an include path for shader compilation
   * @param includePath Path to add to include search paths
   */
  void addIncludePath(const std::string &includePath);

  /**
   * @brief Compile a shader synchronously
   * @param request Compilation request
   * @return Compilation result
   */
  ShaderCompileResult compile(const ShaderCompileRequest &request);

  /**
   * @brief Compile a shader asynchronously using thread pool
   * @param request Compilation request
   * @return Future with compilation result
   */
  std::future<ShaderCompileResult>
  compileAsync(const ShaderCompileRequest &request);

  /**
   * @brief Compile multiple shaders in parallel
   * @param requests Vector of compilation requests
   * @return Vector of compilation results
   */
  std::vector<ShaderCompileResult>
  compileBatch(const std::vector<ShaderCompileRequest> &requests);

  /**
   * @brief Load and compile a shader, caching the result
   * @param sourcePath Path to .slang file
   * @param entryPoint Entry point function name
   * @param stage Shader stage
   * @return Pointer to compiled shader, or nullptr on failure
   */
  CompiledShader *loadShader(const std::string &sourcePath,
                             const std::string &entryPoint, ShaderStage stage);

  /**
   * @brief Get a previously loaded shader
   * @param key Cache key (sourcePath + entryPoint + stage)
   * @return Pointer to shader, or nullptr if not found
   */
  CompiledShader *getShader(const std::string &key);

  /**
   * @brief Check if a shader needs recompilation (source changed)
   * @param key Cache key
   * @return true if shader needs recompilation
   */
  bool needsRecompilation(const std::string &key);

  /**
   * @brief Reload a shader if source has changed
   * @param key Cache key
   * @return true if shader was reloaded
   */
  bool reloadIfNeeded(const std::string &key);

  /**
   * @brief Reload all shaders that have changed
   * @return Number of shaders reloaded
   */
  uint32_t reloadAllChanged();

  /**
   * @brief Clear shader cache
   */
  void clearCache();

  /**
   * @brief Destroy a specific shader from cache
   * @param key Cache key
   * @return true if shader was destroyed
   */
  bool destroyShader(const std::string &key);

  /**
   * @brief Get list of all cached shader keys
   * @return Vector of cache keys
   */
  [[nodiscard]] std::vector<std::string> getCachedShaderKeys() const;

  /**
   * @brief Create a shader module from SPIR-V code
   * @param spirvCode SPIR-V bytecode
   * @return Vulkan shader module
   */
  vk::ShaderModule createShaderModule(const std::vector<uint32_t> &spirvCode);

  /**
   * @brief Generate cache key from shader parameters
   * @param sourcePath Source file path
   * @param entryPoint Entry point name
   * @param stage Shader stage
   * @return Cache key string
   */
  static std::string generateCacheKey(const std::string &sourcePath,
                                      const std::string &entryPoint,
                                      ShaderStage stage);

private:
  bool initializeSlang();
  void shutdownSlang();
  std::string computeFileHash(const std::string &filePath);
  std::string stageToSlangTarget(ShaderStage stage);
  int stageToSlangStage(ShaderStage stage);

  SlangSession *slangSession_ = nullptr;
  vk::Device device_;
  std::string shaderBasePath_;
  std::vector<std::string> includePaths_;

  std::unordered_map<std::string, std::unique_ptr<CompiledShader>> shaderCache_;
  mutable std::mutex cacheMutex_;

  bool initialized_ = false;
};

} // namespace device

#endif // SHADER_MANAGER_H_
