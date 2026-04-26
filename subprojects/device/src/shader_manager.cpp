#include "shader_manager.h"
#include "slang-com-ptr.h"
#include "slang.h"
#include "vulkan_device.h"
#include <algorithm>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <print>
#include <sstream>

namespace device {

// ============================================================================
// CompiledShader Implementation
// ============================================================================

vk::ShaderStageFlagBits CompiledShader::getVkStage() const {
  switch (stage) {
  case ShaderStage::Vertex:
    return vk::ShaderStageFlagBits::eVertex;
  case ShaderStage::Fragment:
    return vk::ShaderStageFlagBits::eFragment;
  case ShaderStage::Geometry:
    return vk::ShaderStageFlagBits::eGeometry;
  case ShaderStage::Compute:
    return vk::ShaderStageFlagBits::eCompute;
  case ShaderStage::TessellationControl:
    return vk::ShaderStageFlagBits::eTessellationControl;
  case ShaderStage::TessellationEvaluation:
    return vk::ShaderStageFlagBits::eTessellationEvaluation;
  default:
    return vk::ShaderStageFlagBits::eVertex;
  }
}

vk::PipelineShaderStageCreateInfo CompiledShader::getStageInfo() const {
  return vk::PipelineShaderStageCreateInfo{
      {},
      getVkStage(),
      module,
      "main"}; // TODO find a better way to deal with this naming problem
               // entryPoint.c_str()};
}

// ============================================================================
// ShaderProgram Hash
// ============================================================================

void ShaderProgram::computeHash() const {
  std::string combined;
  // Process stages in a fixed order so that the hash is deterministic
  auto appendStage = [&](const std::unique_ptr<CompiledShader> &stage) {
    if (stage && stage->isValid) {
      combined += stage->sourceHash;
      combined += ':';
      combined += stage->entryPoint;
      combined += ';';
    }
  };
  appendStage(vertex);
  appendStage(fragment);
  appendStage(geometry);
  appendStage(compute);
  appendStage(tessControl);
  appendStage(tessEval);

  cachedHash_ = std::hash<std::string>{}(combined);
  hashComputed_ = true;
}

std::size_t ShaderProgram::getHash() const {
  if (!hashComputed_) {
    computeHash();
  }
  return cachedHash_;
}

// ============================================================================
// PIMPL for Slang state - keeps Slang headers out of the public header
// ============================================================================

struct ShaderManager::SlangState {
  Slang::ComPtr<slang::IGlobalSession> globalSession;

  // Session description for creating compile sessions
  slang::SessionDesc sessionDesc{};
  slang::TargetDesc targetDesc{};
};

// ============================================================================
// ShaderManager Implementation
// ============================================================================

ShaderManager::ShaderManager() : slangState_(std::make_unique<SlangState>()) {};

ShaderManager::~ShaderManager() { shutdown(); }

bool ShaderManager::initialize(GPUDevice &device) {
  if (initialized_) {
    std::println(stderr, "[ShaderManager] Already initialized");
    return false;
  }

  device_ = &device.getRaiiDevice();

  if (!initializeSlang()) {
    std::println(stderr, "[ShaderManager] Failed to initialize Slang");
    return false;
  }

  // Default shader base path
  shaderBasePath_ = "assets/shaders"; // TODO remove or change interaction for
                                      // loading mod shaders
  includePaths_.push_back(shaderBasePath_);

  // Create SPIR-V cache directory if it doesn't exist
  {
    auto cachePath = std::filesystem::path(shaderBasePath_) / "cache";
    std::error_code ec;
    std::filesystem::create_directories(cachePath, ec);
    if (ec) {
      std::println(
          stderr,
          "[ShaderManager] Warning: could not create cache dir '{}': {}",
          cachePath.string(), ec.message());
    }
  }

  initialized_ = true;
  std::println("[ShaderManager] Initialized successfully");
  return true;
}

void ShaderManager::shutdown() {
  if (!initialized_) {
    return;
  }

  clearCache();
  shutdownSlang();
  device_ = nullptr;
  initialized_ = false;

  std::println("[ShaderManager] Shutdown complete");
}

bool ShaderManager::initializeSlang() {
  std::lock_guard<std::mutex> lock(slangMutex_);
  // Create global session using C++ API
  SlangResult result =
      slang::createGlobalSession(slangState_->globalSession.writeRef());

  if (SLANG_FAILED(result) || !slangState_->globalSession) {
    std::println(stderr,
                 "[ShaderManager] Failed to create Slang global session");
    return false;
  }

  // Configure target for SPIR-V 1.5
  // TODO try and pass this to a config class
  slangState_->targetDesc.format = SLANG_SPIRV;
  slangState_->targetDesc.profile =
      slangState_->globalSession->findProfile("spirv_1_5");
  slangState_->targetDesc.flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;

  return true;
}

void ShaderManager::shutdownSlang() {
  std::lock_guard<std::mutex> lock(slangMutex_);
  slangState_->globalSession.setNull();
}

void ShaderManager::setShaderBasePath(const std::string &basePath) {
  std::lock_guard<std::mutex> lock(slangMutex_);
  shaderBasePath_ = basePath;
  // Update default include path
  if (!includePaths_.empty()) {
    includePaths_[0] = basePath;
  }
}

void ShaderManager::addIncludePath(const std::string &includePath) {
  std::lock_guard<std::mutex> lock(slangMutex_);
  includePaths_.push_back(includePath);
}

slang::ISession *ShaderManager::createCompileSession() {
  // Build search paths array
  std::vector<const char *> searchPathPtrs;
  searchPathPtrs.reserve(includePaths_.size());
  std::ranges::transform(includePaths_, std::back_inserter(searchPathPtrs),
                         [](const std::string &path) { return path.c_str(); });

  // Configure session
  slang::SessionDesc sessionDesc{};
  sessionDesc.targets = &slangState_->targetDesc;
  sessionDesc.targetCount = 1;
  sessionDesc.searchPaths = searchPathPtrs.data();
  sessionDesc.searchPathCount = static_cast<SlangInt>(searchPathPtrs.size());
  sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

  Slang::ComPtr<slang::ISession> session;
  SlangResult result = slangState_->globalSession->createSession(
      sessionDesc, session.writeRef());

  if (SLANG_FAILED(result)) {
    return nullptr;
  }

  // Return raw pointer - caller manages lifetime through ComPtr
  return session.detach();
}

std::string ShaderManager::computeFileHash(const std::string &filePath) {
  std::ifstream file(filePath, std::ios::binary);
  if (!file.is_open()) {
    return "";
  }

  // Simple hash based on file content and modification time
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();

  std::filesystem::path path(filePath);
  auto lastWrite = std::filesystem::last_write_time(path);
  auto timeValue = std::chrono::duration_cast<std::chrono::seconds>(
                       lastWrite.time_since_epoch())
                       .count();

  // Simple hash combining content length, first/last bytes, and modification
  // time
  size_t hash = content.size();
  hash ^= std::hash<long long>{}(timeValue);
  if (!content.empty()) {
    hash ^= std::hash<char>{}(content.front()) << 8;
    hash ^= std::hash<char>{}(content.back()) << 16;
  }

  std::stringstream ss;
  ss << std::hex << hash;
  file.close();
  return ss.str();
}

SlangStage ShaderManager::stageToSlangStage(ShaderStage stage) {
  switch (stage) {
  case ShaderStage::Vertex:
    return SlangStage::SLANG_STAGE_VERTEX;
  case ShaderStage::Fragment:
    return SlangStage::SLANG_STAGE_FRAGMENT;
  case ShaderStage::Geometry:
    return SlangStage::SLANG_STAGE_GEOMETRY;
  case ShaderStage::Compute:
    return SlangStage::SLANG_STAGE_COMPUTE;
  case ShaderStage::TessellationControl:
    return SlangStage::SLANG_STAGE_HULL;
  case ShaderStage::TessellationEvaluation:
    return SlangStage::SLANG_STAGE_DOMAIN;
  default:
    return SlangStage::SLANG_STAGE_VERTEX;
  }
}

ShaderManager::ShaderCompileResult
ShaderManager::compile(const ShaderCompileRequest &request) {
  ShaderCompileResult result;
  result.success = false;

  // Lock for session creation (global session access)
  Slang::ComPtr<slang::ISession> session;
  {
    std::lock_guard<std::mutex> lock(slangMutex_);
    if (!slangState_->globalSession) {
      result.errorMessage = "Global slang session not initialized";
      return result;
    }
    session = createCompileSession();
  }

  if (!session) {
    result.errorMessage = "Failed to create Slang compile session";
    return result;
  }

  // Construct full path
  std::string fullPath = request.sourcePath;
  if (!std::filesystem::path(fullPath).is_absolute()) {
    fullPath = shaderBasePath_ + "/" + request.sourcePath;
  }

  // Validate path to prevent directory traversal attacks
  auto normalizedPath = std::filesystem::path(fullPath).lexically_normal();
  auto basePath = std::filesystem::path(shaderBasePath_).lexically_normal();
  std::string normalizedStr = normalizedPath.string();
  std::string baseStr = basePath.string();
  if (normalizedStr.length() < baseStr.length() ||
      !normalizedStr.starts_with(baseStr)) {
    result.errorMessage =
        "Invalid shader path: access denied (path traversal attempt)";
    return result;
  }
  fullPath = normalizedStr;

  // Check if file exists
  if (!std::filesystem::exists(fullPath)) {
    result.errorMessage = "Shader file not found: " + fullPath;
    return result;
  }

  // Compute source hash for hot reload detection
  result.sourceHash = computeFileHash(fullPath);

  // Load module using C++ API
  Slang::ComPtr<slang::IBlob> diagnosticsBlob;
  slang::IModule *module =
      session->loadModule(fullPath.c_str(), diagnosticsBlob.writeRef());

  if (module == nullptr) {
    if (diagnosticsBlob != nullptr) {
      result.errorMessage =
          static_cast<const char *>(diagnosticsBlob->getBufferPointer());
    } else {
      result.errorMessage = "Failed to load module: " + fullPath;
    }
    return result;
  }

  // Find entry point
  Slang::ComPtr<slang::IEntryPoint> entryPoint;
  SlangResult findResult = module->findEntryPointByName(
      request.entryPoint.c_str(), entryPoint.writeRef());

  if (SLANG_FAILED(findResult) || entryPoint == nullptr) {
    // Try with explicit stage if not marked in source
    Slang::ComPtr<slang::IBlob> epDiagnostics;
    findResult = module->findAndCheckEntryPoint(
        request.entryPoint.c_str(), stageToSlangStage(request.stage),
        entryPoint.writeRef(), epDiagnostics.writeRef());

    if (SLANG_FAILED(findResult) || entryPoint == nullptr) {
      result.errorMessage = "Entry point not found: " + request.entryPoint;
      if (epDiagnostics != nullptr) {
        result.errorMessage += "\n";
        result.errorMessage +=
            static_cast<const char *>(epDiagnostics->getBufferPointer());
      }
      return result;
    }
  }

  // Create composite component type
  std::array<slang::IComponentType *, 2> components = {module,
                                                       entryPoint.get()};
  Slang::ComPtr<slang::IComponentType> composedProgram;

  SlangResult composeResult = session->createCompositeComponentType(
      components.data(), static_cast<SlangInt>(components.size()),
      composedProgram.writeRef(), diagnosticsBlob.writeRef());

  if (SLANG_FAILED(composeResult)) {
    if (diagnosticsBlob != nullptr) {
      result.errorMessage =
          static_cast<const char *>(diagnosticsBlob->getBufferPointer());
    } else {
      result.errorMessage = "Failed to compose shader program";
    }
    return result;
  }

  // Link the program
  Slang::ComPtr<slang::IComponentType> linkedProgram;
  SlangResult linkResult = composedProgram->link(linkedProgram.writeRef(),
                                                 diagnosticsBlob.writeRef());

  if (SLANG_FAILED(linkResult)) {
    if (diagnosticsBlob != nullptr) {
      result.errorMessage =
          static_cast<const char *>(diagnosticsBlob->getBufferPointer());
    } else {
      result.errorMessage = "Failed to link shader program";
    }
    return result;
  }

  // Get compiled SPIR-V code
  Slang::ComPtr<slang::IBlob> codeBlob;
  SlangResult codeResult = linkedProgram->getEntryPointCode(
      0, // Entry point index
      0, // Target index
      codeBlob.writeRef(), diagnosticsBlob.writeRef());

  if (SLANG_FAILED(codeResult) || codeBlob == nullptr) {
    if (diagnosticsBlob != nullptr) {
      result.errorMessage =
          static_cast<const char *>(diagnosticsBlob->getBufferPointer());
    } else {
      result.errorMessage = "Failed to get SPIR-V code";
    }
    return result;
  }

  // Validate and copy SPIR-V
  size_t codeSize = codeBlob->getBufferSize();
  if (codeSize == 0 || codeSize % sizeof(uint32_t) != 0) {
    result.errorMessage = "Invalid SPIR-V code size";
    return result;
  }

  result.spirvCode.resize(codeSize / sizeof(uint32_t));
  std::memcpy(result.spirvCode.data(), codeBlob->getBufferPointer(), codeSize);

  result.success = true;
  return result;
}

// ============================================================================
// Tag-based shader management
// ============================================================================

std::expected<ShaderProgram *, ShaderError>
ShaderManager::acquire(const ShaderTag *tag) {
  if (!initialized_ || !tag) {
    return std::unexpected(
        ShaderError{"ShaderManager not initialized or null tag"});
  }

  // Check if already in registry
  if (auto *program = shaderRegistry_.get(tag)) {
    program->refCount++;
    return program;
  }

  // Compile all stages specified in the tag
  auto program = std::make_unique<ShaderProgram>(*tag);
  bool hasFailure = false;

  if (tag->vertexEntry) {
    program->vertex = compileStageWithCache(
        tag->name, tag->sourcePath, tag->vertexEntry, ShaderStage::Vertex);
    if (!program->vertex || !program->vertex->isValid) {
      std::println(stderr,
                   "[ShaderManager] Failed to compile vertex stage '{}' "
                   "with entry point '{}' in '{}'",
                   tag->name, tag->vertexEntry, tag->sourcePath);
      hasFailure = true;
    }
  }
  if (tag->fragmentEntry) {
    program->fragment = compileStageWithCache(
        tag->name, tag->sourcePath, tag->fragmentEntry, ShaderStage::Fragment);
    if (!program->fragment || !program->fragment->isValid) {
      std::println(stderr,
                   "[ShaderManager] Failed to compile fragment stage '{}' "
                   "with entry point '{}' in '{}'",
                   tag->name, tag->fragmentEntry, tag->sourcePath);
      hasFailure = true;
    }
  }
  if (tag->geometryEntry) {
    program->geometry = compileStageWithCache(
        tag->name, tag->sourcePath, tag->geometryEntry, ShaderStage::Geometry);
    if (!program->geometry || !program->geometry->isValid) {
      std::println(stderr,
                   "[ShaderManager] Failed to compile geometry stage '{}' "
                   "with entry point '{}' in '{}'",
                   tag->name, tag->geometryEntry, tag->sourcePath);
      hasFailure = true;
    }
  }
  if (tag->computeEntry) {
    program->compute = compileStageWithCache(
        tag->name, tag->sourcePath, tag->computeEntry, ShaderStage::Compute);
    if (!program->compute || !program->compute->isValid) {
      std::println(stderr,
                   "[ShaderManager] Failed to compile compute stage '{}' "
                   "with entry point '{}' in '{}'",
                   tag->name, tag->computeEntry, tag->sourcePath);
      hasFailure = true;
    }
  }
  if (tag->tessCtrlEntry) {
    program->tessControl =
        compileStageWithCache(tag->name, tag->sourcePath, tag->tessCtrlEntry,
                              ShaderStage::TessellationControl);
    if (!program->tessControl || !program->tessControl->isValid) {
      std::println(stderr,
                   "[ShaderManager] Failed to compile tessellation "
                   "control stage '{}' "
                   "with entry point '{}' in '{}'",
                   tag->name, tag->tessCtrlEntry, tag->sourcePath);
      hasFailure = true;
    }
  }
  if (tag->tessEvalEntry) {
    program->tessEval =
        compileStageWithCache(tag->name, tag->sourcePath, tag->tessEvalEntry,
                              ShaderStage::TessellationEvaluation);
    if (!program->tessEval || !program->tessEval->isValid) {
      std::println(stderr,
                   "[ShaderManager] Failed to compile tessellation evaluation "
                   "stage '{}' with entry point '{}' in '{}'",
                   tag->name, tag->tessEvalEntry, tag->sourcePath);
      hasFailure = true;
    }
  }

  // Validate that at least one stage compiled successfully
  auto stageInfos = program->getStageInfos();
  if (stageInfos.empty()) {
    return std::unexpected(
        ShaderError{std::string("No valid shader stages compiled for '") +
                    tag->name + "' from source '" + tag->sourcePath + "'"});
  }

  if (hasFailure) {
    std::println(stderr,
                 "[ShaderManager] Shader program '{}' compiled with errors "
                 "({} of requested stages succeeded)",
                 tag->name, stageInfos.size());
  }

  program->refCount = 1;
  program->compiled = true;

  (void)program->getHash();

  ShaderProgram *ptr = program.get();
  shaderRegistry_.add(tag, std::move(program));

  std::println(
      "[ShaderManager] Acquired shader program '{}': {} stage(s) ready",
      tag->name, stageInfos.size());
  return ptr;
}

void ShaderManager::release(const ShaderTag *tag) {
  if (!tag) {
    return;
  }

  auto *program = shaderRegistry_.get(tag);
  if (!program) {
    return;
  }

  if (program->refCount > 0) {
    program->refCount--;
  }

  // When no longer in use, discard to free memory
  if (program->refCount == 0) {
    std::println("[ShaderManager] Releasing unused shader program: {}",
                 tag->name);
    shaderRegistry_.remove(tag);
  }
}

ShaderProgram *ShaderManager::getProgram(const ShaderTag *tag) {
  if (!tag) {
    return nullptr;
  }
  return shaderRegistry_.get(tag);
}

std::unique_ptr<CompiledShader>
ShaderManager::compileStage(const std::string &sourcePath,
                            const std::string &entryPoint, ShaderStage stage) {
  ShaderCompileRequest request;
  request.sourcePath = sourcePath;
  request.entryPoint = entryPoint;
  request.stage = stage;

  auto result = compile(request);
  if (!result.success) {
    std::println(stderr, "[ShaderManager] Failed to compile stage {}:{} - {}",
                 sourcePath, entryPoint, result.errorMessage);
    return nullptr;
  }

  // Create shader module
  auto module = createShaderModule(result.spirvCode);
  if (module == nullptr) {
    return nullptr;
  }

  // Create and cache compiled shader
  auto shader = std::make_unique<CompiledShader>();
  shader->module = std::move(module);
  shader->stage = stage;
  shader->entryPoint = entryPoint;
  shader->sourcePath = sourcePath;
  shader->sourceHash = result.sourceHash;
  shader->isValid = true;

  return shader;
}

vk::raii::ShaderModule
ShaderManager::createShaderModule(const std::vector<uint32_t> &spirvCode) {
  if (!device_ || spirvCode.empty()) {
    return nullptr;
  }

  vk::ShaderModuleCreateInfo createInfo{
      {}, spirvCode.size() * sizeof(uint32_t), spirvCode.data()};

  try {
    return device_->createShaderModule(createInfo);
  } catch (const vk::SystemError &e) {
    std::println(stderr, "[ShaderManager] Failed to create shader module: {}",
                 e.what());
    return nullptr;
  }
}

void ShaderManager::clearCache() { shaderRegistry_.clear(); }

// ============================================================================
// SPIR-V Disk Cache
// ============================================================================

std::string ShaderManager::stageSuffix(ShaderStage stage) {
  switch (stage) {
  case ShaderStage::Vertex:
    return "_vert";
  case ShaderStage::Fragment:
    return "_frag";
  case ShaderStage::Geometry:
    return "_geom";
  case ShaderStage::Compute:
    return "_comp";
  case ShaderStage::TessellationControl:
    return "_tesc";
  case ShaderStage::TessellationEvaluation:
    return "_tese";
  default:
    return "_unknown";
  }
}

std::string ShaderManager::getCachePath(const std::string &name,
                                        const std::string &hash,
                                        ShaderStage stage) const {
  return shaderBasePath_ + "/cache/" + name + "_" + hash + stageSuffix(stage) +
         ".spv";
}

bool ShaderManager::loadCachedSpirv(const std::string &cachePath,
                                    std::vector<uint32_t> &spirvOut) {
  if (!std::filesystem::exists(cachePath)) {
    return false;
  }

  std::ifstream file(cachePath, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return false;
  }

  auto fileSize = file.tellg();
  if (fileSize <= 0 || static_cast<size_t>(fileSize) % sizeof(uint32_t) != 0) {
    return false;
  }

  file.seekg(0, std::ios::beg);
  spirvOut.resize(static_cast<size_t>(fileSize) / sizeof(uint32_t));
  file.read(reinterpret_cast<char *>(spirvOut.data()),
            static_cast<std::streamsize>(fileSize));
  return file.good();
}

bool ShaderManager::writeCachedSpirv(const std::string &cachePath,
                                     const std::vector<uint32_t> &spirv) {
  std::ofstream file(cachePath, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    return false;
  }

  file.write(reinterpret_cast<const char *>(spirv.data()),
             static_cast<std::streamsize>(spirv.size() * sizeof(uint32_t)));
  return file.good();
}

std::unique_ptr<CompiledShader> ShaderManager::compileStageWithCache(
    const std::string &name, const std::string &sourcePath,
    const std::string &entryPoint, ShaderStage stage) {

  // Construct full path to compute hash
  std::string fullPath = sourcePath;
  if (!std::filesystem::path(fullPath).is_absolute()) {
    fullPath = shaderBasePath_ + "/" + sourcePath;
  }

  std::string sourceHash = computeFileHash(fullPath);

  // Try loading from cache
  std::string cachePath = getCachePath(name, sourceHash, stage);
  std::vector<uint32_t> spirvCode;

  {
    std::lock_guard<std::mutex> lock(slangMutex_);
    if (loadCachedSpirv(cachePath, spirvCode)) {
      // Create shader module from cached SPIR-V
      auto module = createShaderModule(spirvCode);
      if (module != nullptr) {
        auto shader = std::make_unique<CompiledShader>();
        shader->module = std::move(module);
        shader->stage = stage;
        shader->entryPoint = entryPoint;
        shader->sourcePath = sourcePath;
        shader->sourceHash = sourceHash;
        shader->isValid = true;
        std::println("[ShaderManager] Loaded cached SPIR-V for '{}/{}'", name,
                     stageSuffix(stage).substr(1));
        return shader;
      }
    }
  }

  // Cache miss or invalid — compile via Slang
  auto shader = compileStage(sourcePath, entryPoint, stage);
  if (!shader || !shader->isValid) {
    return shader;
  }

  // Write compiled SPIR-V to cache (recompile to get SPIR-V bytes)
  ShaderCompileRequest request;
  request.sourcePath = sourcePath;
  request.entryPoint = entryPoint;
  request.stage = stage;
  auto result = compile(request);
  if (result.success) {
    std::lock_guard<std::mutex> lock(slangMutex_);
    if (writeCachedSpirv(cachePath, result.spirvCode)) {
      std::println("[ShaderManager] Compiled and cached '{}/{}'", name,
                   stageSuffix(stage).substr(1));
    }
  }

  return shader;
}

} // namespace device
