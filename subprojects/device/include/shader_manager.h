#ifndef SHADER_MANAGER_H_
#define SHADER_MANAGER_H_

#include "device_export.h"
#include "resource_registry.h"
#include "slang.h"
#include "vulkan/vulkan_raii.hpp"
#include "vulkan_device.h"
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

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
  const char *sourcePath;       // Path to .slang file
  const char *vertexEntry;      // Vertex shader entry point (nullptr if unused)
  const char *fragmentEntry;    // Fragment shader entry point (nullptr if unused)
  const char *geometryEntry;    // Geometry shader entry point (nullptr if unused)
  const char *computeEntry;     // Compute shader entry point (nullptr if unused)
  const char *tessCtrlEntry;    // Tessellation control entry (nullptr if unused)
  const char *tessEvalEntry;    // Tessellation evaluation entry (nullptr if unused)

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
 * @brief Manages Slang shader compilation at runtime using the tag system
 *
 * Features:
 * - Tag-based shader identification via ResourceRegistry
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

  // ========== Tag-based shader management ==========

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

  // ========== Legacy string-key based interface ==========

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
   * @brief Clear shader cache (both tag-based registry and legacy string-key cache)
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
  vk::raii::ShaderModule
  createShaderModule(const std::vector<uint32_t> &spirvCode);

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

  // Compile a single stage for a shader program
  std::unique_ptr<CompiledShader> compileStage(const std::string &sourcePath,
                                               const std::string &entryPoint,
                                               ShaderStage stage);

  // Create a thread-local or locked session for compilation
  slang::ISession *createCompileSession();
  mutable std::mutex slangMutex_;

  static std::string computeFileHash(const std::string &filePath);
  static std::string stageToSlangTarget(ShaderStage stage);
  static SlangStage stageToSlangStage(ShaderStage stage);

  // Use opaque pointer pattern (PIMPL) for Slang types
  struct SlangState;
  std::unique_ptr<SlangState> slangState_;

  const vk::raii::Device *device_ = nullptr;
  std::string shaderBasePath_;
  std::vector<std::string> includePaths_;

  // Tag-based shader registry
  core::ResourceRegistry<ShaderTag, ShaderProgram> shaderRegistry_;

  // Legacy string-key cache (kept for backward compatibility)
  std::unordered_map<std::string, std::unique_ptr<CompiledShader>> shaderCache_;
  mutable std::mutex cacheMutex_;

  bool initialized_ = false;
};

} // namespace device

#endif // SHADER_MANAGER_H_
