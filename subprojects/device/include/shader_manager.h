#ifndef SHADER_MANAGER_H_
#define SHADER_MANAGER_H_

#include "device_export.h"
#include "resource_registry.h"
#include "slang.h"
#include "vulkan/vulkan_raii.hpp"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace device {

// Forward declaration
class GPUDevice;

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
 * @brief Tag for identifying shaders in the resource system
 *
 * Stores metadata about a shader program (source file, entry points, stages).
 * Must have static storage duration (constexpr, static, or global)
 * when used with ResourceRegistry.
 *
 * Example:
 * @code
 *   constexpr ShaderTag TRIANGLE_SHADER{"triangle", "triangle.slang",
 *     "vertMain", "fragMain", "geomMain"};
 * @endcode
 */
struct DEVICE_API ShaderTag {
  const char *name;
  const char *sourcePath;    // Path to .slang file
  const char *vertexEntry;   // Vertex shader entry point (nullptr if unused)
  const char *fragmentEntry; // Fragment shader entry point (nullptr if unused)
  const char *geometryEntry; // Geometry shader entry point (nullptr if unused)
  const char *computeEntry;  // Compute shader entry point (nullptr if unused)
  const char *tessCtrlEntry; // Tessellation control entry (nullptr if unused)
  const char
      *tessEvalEntry; // Tessellation evaluation entry (nullptr if unused)

  constexpr ShaderTag(const char *n, const char *src,
                      const char *vert = nullptr, const char *frag = nullptr,
                      const char *geom = nullptr, const char *comp = nullptr,
                      const char *tesc = nullptr, const char *tese = nullptr)
      : name(n), sourcePath(src), vertexEntry(vert), fragmentEntry(frag),
        geometryEntry(geom), computeEntry(comp), tessCtrlEntry(tesc),
        tessEvalEntry(tese) {}
};

/**
 * @brief Compiled shader module information
 */
struct DEVICE_API CompiledShader {
  vk::raii::ShaderModule module = nullptr;
  ShaderStage stage;
  std::string entryPoint;
  std::string sourcePath; // Original source path
  std::string sourceHash; // Hash for hot reload detection
  bool isValid = false;

  [[nodiscard]] vk::ShaderStageFlagBits getVkStage() const;
  [[nodiscard]] vk::PipelineShaderStageCreateInfo getStageInfo() const;
};

/**
 * @brief A shader program containing all compiled stages for a single tag
 *
 * Stored in the ResourceRegistry. When in use, all stages are compiled and
 * stored. When no longer in use, it can be discarded to free memory.
 */
struct DEVICE_API ShaderProgram {
  std::unique_ptr<CompiledShader> vertex;
  std::unique_ptr<CompiledShader> fragment;
  std::unique_ptr<CompiledShader> geometry;
  std::unique_ptr<CompiledShader> compute;
  std::unique_ptr<CompiledShader> tessControl;
  std::unique_ptr<CompiledShader> tessEval;
  uint32_t refCount = 0; // Reference count for memory management
  bool compiled = false;

  explicit ShaderProgram(const ShaderTag & /*tag*/) {}

  /**
   * @brief Get all valid pipeline shader stage create infos
   * @return Vector of shader stage create infos for pipeline creation
   */
  [[nodiscard]] std::vector<vk::PipelineShaderStageCreateInfo>
  getStageInfos() const {
    std::vector<vk::PipelineShaderStageCreateInfo> stages;
    if (vertex && vertex->isValid)
      stages.push_back(vertex->getStageInfo());
    if (fragment && fragment->isValid)
      stages.push_back(fragment->getStageInfo());
    if (geometry && geometry->isValid)
      stages.push_back(geometry->getStageInfo());
    if (compute && compute->isValid)
      stages.push_back(compute->getStageInfo());
    if (tessControl && tessControl->isValid)
      stages.push_back(tessControl->getStageInfo());
    if (tessEval && tessEval->isValid)
      stages.push_back(tessEval->getStageInfo());
    return stages;
  }
};

/**
 * @brief Manages Slang shader compilation at runtime using tag system
 *
 * Features:
 * - Runtime compilation of Slang shaders to SPIR-V
 * - Support for multiple entry points in single .slang file
 * - Reference-counted caching: shaders compiled when in use, discarded when not
 * - Parallel compilation using thread pools
 * - Hot reload support (comparing source hashes)
 * - Shaders can be shared across materials to save memory
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
   * @brief Acquire a shader program by tag, compiling it if needed
   *
   * Increments the reference count. The shader will remain compiled
   * as long as at least one reference exists.
   *
   * @param tag Shader tag identifying the program
   * @return Pointer to compiled shader program, or nullptr on failure
   */
  ShaderProgram *acquire(const ShaderTag *tag);

  /**
   * @brief Release a shader program, potentially freeing memory
   *
   * Decrements the reference count. When the count reaches zero,
   * the compiled shaders are discarded to save memory.
   *
   * @param tag Shader tag to release
   */
  void release(const ShaderTag *tag);

  /**
   * @brief Get a shader program by tag without changing reference count
   * @param tag Shader tag
   * @return Pointer to shader program, or nullptr if not loaded
   */
  ShaderProgram *getProgram(const ShaderTag *tag);

  /**
   * @brief Get the shader registry for direct access
   * @return Reference to the shader registry
   */
  [[nodiscard]] core::ResourceRegistry<ShaderTag, ShaderProgram> &
  getRegistry() {
    return shaderRegistry_;
  }

  /**
   * @brief Clear all compiled shaders from the registry
   */
  void clearCache();

private:
  bool initializeSlang();
  void shutdownSlang();

  // ========== Internal compilation helpers ==========

  /**
   * @brief Shader compilation request (internal)
   */
  struct ShaderCompileRequest {
    std::string sourcePath;                // Path to .slang file
    std::string entryPoint;                // Entry point function name
    ShaderStage stage;                     // Target shader stage
    std::vector<std::string> defines;      // Preprocessor defines
    std::vector<std::string> includePaths; // Include search paths
  };

  /**
   * @brief Result of shader compilation (internal)
   */
  struct ShaderCompileResult {
    bool success = false;
    std::string errorMessage;
    std::vector<uint32_t> spirvCode; // Compiled SPIR-V bytecode
    std::string sourceHash;
  };

  /**
   * @brief Compile a shader from a request (internal)
   * @param request Compilation request
   * @return Compilation result
   */
  ShaderCompileResult compile(const ShaderCompileRequest &request);

  /**
   * @brief Create a Vulkan shader module from SPIR-V code (internal)
   * @param spirvCode SPIR-V bytecode
   * @return Vulkan shader module
   */
  vk::raii::ShaderModule
  createShaderModule(const std::vector<uint32_t> &spirvCode);

  // Compile a single stage for a shader program
  std::unique_ptr<CompiledShader> compileStage(const std::string &sourcePath,
                                               const std::string &entryPoint,
                                               ShaderStage stage);

  // Create a thread-local or locked session for compilation
  slang::ISession *createCompileSession();
  mutable std::mutex slangMutex_;

  static std::string computeFileHash(const std::string &filePath);
  static SlangStage stageToSlangStage(ShaderStage stage);

  // Use opaque pointer pattern (PIMPL) for Slang types
  struct SlangState;
  std::unique_ptr<SlangState> slangState_;

  const vk::raii::Device *device_ = nullptr;
  std::string shaderBasePath_;
  std::vector<std::string> includePaths_;

  // Tag-based shader registry
  core::ResourceRegistry<ShaderTag, ShaderProgram> shaderRegistry_;

  bool initialized_ = false;
};

} // namespace device

#endif // SHADER_MANAGER_H_
