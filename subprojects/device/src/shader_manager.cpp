#include "shader_manager.h"
#include "thread_manager.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <slang.h>

namespace device {

// ============================================================================
// CompiledShader Implementation
// ============================================================================

vk::ShaderStageFlagBits CompiledShader::getVkStage() const {
    switch (stage) {
        case ShaderStage::Vertex: return vk::ShaderStageFlagBits::eVertex;
        case ShaderStage::Fragment: return vk::ShaderStageFlagBits::eFragment;
        case ShaderStage::Geometry: return vk::ShaderStageFlagBits::eGeometry;
        case ShaderStage::TessellationControl: return vk::ShaderStageFlagBits::eTessellationControl;
        case ShaderStage::TessellationEvaluation: return vk::ShaderStageFlagBits::eTessellationEvaluation;
        case ShaderStage::Compute: return vk::ShaderStageFlagBits::eCompute;
        default: return vk::ShaderStageFlagBits::eVertex;
    }
}

vk::PipelineShaderStageCreateInfo CompiledShader::getStageInfo() const {
    return vk::PipelineShaderStageCreateInfo{
        {},
        getVkStage(),
        module,
        entryPoint.c_str()
    };
}

// ============================================================================
// ShaderManager Implementation
// ============================================================================

ShaderManager::ShaderManager() = default;

ShaderManager::~ShaderManager() {
    shutdown();
}

bool ShaderManager::initialize(GPUDevice& device) {
    if (initialized_) {
        std::cerr << "[ShaderManager] Already initialized" << std::endl;
        return false;
    }

    device_ = device.getDevice();

    if (!initializeSlang()) {
        std::cerr << "[ShaderManager] Failed to initialize Slang" << std::endl;
        return false;
    }

    // Default shader base path
    shaderBasePath_ = "assets/shaders";
    includePaths_.push_back(shaderBasePath_);

    initialized_ = true;
    std::cout << "[ShaderManager] Initialized successfully" << std::endl;
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

    std::cout << "[ShaderManager] Shutdown complete" << std::endl;
}

bool ShaderManager::initializeSlang() {
    slangSession_ = spCreateSession(nullptr);
    if (!slangSession_) {
        std::cerr << "[ShaderManager] Failed to create Slang session" << std::endl;
        return false;
    }
    return true;
}

void ShaderManager::shutdownSlang() {
    if (slangSession_) {
        spDestroySession(slangSession_);
        slangSession_ = nullptr;
    }
}

void ShaderManager::setShaderBasePath(const std::string& basePath) {
    shaderBasePath_ = basePath;
    // Update default include path
    if (!includePaths_.empty()) {
        includePaths_[0] = basePath;
    }
}

void ShaderManager::addIncludePath(const std::string& includePath) {
    includePaths_.push_back(includePath);
}

std::string ShaderManager::computeFileHash(const std::string& filePath) {
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
        lastWrite.time_since_epoch()).count();

    // Simple hash combining content length, first/last bytes, and modification time
    size_t hash = content.size();
    hash ^= std::hash<long long>{}(timeValue);
    if (!content.empty()) {
        hash ^= std::hash<char>{}(content.front()) << 8;
        hash ^= std::hash<char>{}(content.back()) << 16;
    }

    std::stringstream ss;
    ss << std::hex << hash;
    return ss.str();
}

std::string ShaderManager::stageToSlangTarget(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex: return "vertex";
        case ShaderStage::Fragment: return "fragment";
        case ShaderStage::Geometry: return "geometry";
        case ShaderStage::TessellationControl: return "hull";
        case ShaderStage::TessellationEvaluation: return "domain";
        case ShaderStage::Compute: return "compute";
        default: return "vertex";
    }
}

int ShaderManager::stageToSlangStage(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex: return SLANG_STAGE_VERTEX;
        case ShaderStage::Fragment: return SLANG_STAGE_FRAGMENT;
        case ShaderStage::Geometry: return SLANG_STAGE_GEOMETRY;
        case ShaderStage::TessellationControl: return SLANG_STAGE_HULL;
        case ShaderStage::TessellationEvaluation: return SLANG_STAGE_DOMAIN;
        case ShaderStage::Compute: return SLANG_STAGE_COMPUTE;
        default: return SLANG_STAGE_VERTEX;
    }
}

ShaderCompileResult ShaderManager::compile(const ShaderCompileRequest& request) {
    ShaderCompileResult result;
    result.success = false;

    if (!slangSession_) {
        result.errorMessage = "Slang session not initialized";
        return result;
    }

    // Construct full path
    std::string fullPath = request.sourcePath;
    if (!std::filesystem::path(fullPath).is_absolute()) {
        fullPath = shaderBasePath_ + "/" + request.sourcePath;
    }

    // Check if file exists
    if (!std::filesystem::exists(fullPath)) {
        result.errorMessage = "Shader file not found: " + fullPath;
        return result;
    }

    // Compute source hash for hot reload detection
    result.sourceHash = computeFileHash(fullPath);

    // Create compile request
    SlangCompileRequest* slangRequest = spCreateCompileRequest(slangSession_);
    if (!slangRequest) {
        result.errorMessage = "Failed to create Slang compile request";
        return result;
    }

    // Set target to SPIR-V
    int targetIndex = spAddCodeGenTarget(slangRequest, SLANG_SPIRV);
    spSetTargetProfile(slangRequest, targetIndex, spFindProfile(slangSession_, "spirv_1_5"));

    // Add search paths
    for (const auto& path : includePaths_) {
        spAddSearchPath(slangRequest, path.c_str());
    }

    // Add preprocessor defines
    for (const auto& define : request.defines) {
        auto pos = define.find('=');
        if (pos != std::string::npos) {
            std::string name = define.substr(0, pos);
            std::string value = define.substr(pos + 1);
            spAddPreprocessorDefine(slangRequest, name.c_str(), value.c_str());
        } else {
            spAddPreprocessorDefine(slangRequest, define.c_str(), "1");
        }
    }

    // Add source file as translation unit
    int translationUnitIndex = spAddTranslationUnit(slangRequest, SLANG_SOURCE_LANGUAGE_SLANG, nullptr);
    spAddTranslationUnitSourceFile(slangRequest, translationUnitIndex, fullPath.c_str());

    // Add entry point
    spAddEntryPoint(slangRequest, translationUnitIndex, request.entryPoint.c_str(),
                    static_cast<SlangStage>(stageToSlangStage(request.stage)));

    // Compile
    int compileResult = spCompile(slangRequest);
    if (compileResult != 0) {
        const char* diagnostics = spGetDiagnosticOutput(slangRequest);
        result.errorMessage = diagnostics ? diagnostics : "Unknown compilation error";
        spDestroyCompileRequest(slangRequest);
        return result;
    }

    // Get SPIR-V output
    size_t codeSize = 0;
    const void* code = spGetEntryPointCode(slangRequest, 0, &codeSize);
    if (!code || codeSize == 0) {
        result.errorMessage = "Failed to get SPIR-V code";
        spDestroyCompileRequest(slangRequest);
        return result;
    }

    // Copy SPIR-V code
    result.spirvCode.resize(codeSize / sizeof(uint32_t));
    std::memcpy(result.spirvCode.data(), code, codeSize);

    spDestroyCompileRequest(slangRequest);

    result.success = true;
    return result;
}

std::future<ShaderCompileResult> ShaderManager::compileAsync(const ShaderCompileRequest& request) {
    // Try to use worker thread pool for async compilation
    if (core::ThreadManager::instance().hasPool("worker")) {
        return core::ThreadManager::instance().submitTo("worker", [this, request]() {
            return compile(request);
        });
    }

    // Fall back to std::async
    return std::async(std::launch::async, [this, request]() {
        return compile(request);
    });
}

std::vector<ShaderCompileResult> ShaderManager::compileBatch(const std::vector<ShaderCompileRequest>& requests) {
    std::vector<ShaderCompileResult> results;
    results.reserve(requests.size());

    // Use thread pool for parallel compilation if available
    if (requests.size() > 1 && core::ThreadManager::instance().hasPool("worker")) {
        std::vector<std::future<ShaderCompileResult>> futures;
        futures.reserve(requests.size());

        for (const auto& request : requests) {
            futures.push_back(compileAsync(request));
        }

        for (auto& future : futures) {
            results.push_back(future.get());
        }
    } else {
        // Compile sequentially
        for (const auto& request : requests) {
            results.push_back(compile(request));
        }
    }

    return results;
}

vk::ShaderModule ShaderManager::createShaderModule(const std::vector<uint32_t>& spirvCode) {
    if (!device_ || spirvCode.empty()) {
        return nullptr;
    }

    vk::ShaderModuleCreateInfo createInfo{
        {},
        spirvCode.size() * sizeof(uint32_t),
        spirvCode.data()
    };

    try {
        return device_.createShaderModule(createInfo);
    } catch (const vk::SystemError& e) {
        std::cerr << "[ShaderManager] Failed to create shader module: " << e.what() << std::endl;
        return nullptr;
    }
}

std::string ShaderManager::generateCacheKey(const std::string& sourcePath,
                                            const std::string& entryPoint,
                                            ShaderStage stage) {
    return sourcePath + ":" + entryPoint + ":" + std::to_string(static_cast<int>(stage));
}

CompiledShader* ShaderManager::loadShader(const std::string& sourcePath,
                                          const std::string& entryPoint,
                                          ShaderStage stage) {
    std::string key = generateCacheKey(sourcePath, entryPoint, stage);

    // Check cache first
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = shaderCache_.find(key);
        if (it != shaderCache_.end() && it->second->isValid) {
            return it->second.get();
        }
    }

    // Compile shader
    ShaderCompileRequest request;
    request.sourcePath = sourcePath;
    request.entryPoint = entryPoint;
    request.stage = stage;

    auto result = compile(request);
    if (!result.success) {
        std::cerr << "[ShaderManager] Failed to compile " << sourcePath << ":" << entryPoint
                  << " - " << result.errorMessage << std::endl;
        return nullptr;
    }

    // Create shader module
    auto module = createShaderModule(result.spirvCode);
    if (!module) {
        return nullptr;
    }

    // Create and cache compiled shader
    auto shader = std::make_unique<CompiledShader>();
    shader->module = module;
    shader->stage = stage;
    shader->entryPoint = entryPoint;
    shader->sourcePath = sourcePath;
    shader->sourceHash = result.sourceHash;
    shader->isValid = true;

    std::lock_guard<std::mutex> lock(cacheMutex_);
    shaderCache_[key] = std::move(shader);
    return shaderCache_[key].get();
}

CompiledShader* ShaderManager::getShader(const std::string& key) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = shaderCache_.find(key);
    if (it != shaderCache_.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool ShaderManager::needsRecompilation(const std::string& key) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = shaderCache_.find(key);
    if (it == shaderCache_.end()) {
        return true;
    }

    // Check if source file has changed
    std::string fullPath = it->second->sourcePath;
    if (!std::filesystem::path(fullPath).is_absolute()) {
        fullPath = shaderBasePath_ + "/" + it->second->sourcePath;
    }

    std::string currentHash = computeFileHash(fullPath);
    return currentHash != it->second->sourceHash;
}

bool ShaderManager::reloadIfNeeded(const std::string& key) {
    if (!needsRecompilation(key)) {
        return false;
    }

    CompiledShader* existing = nullptr;
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = shaderCache_.find(key);
        if (it != shaderCache_.end()) {
            existing = it->second.get();
        }
    }

    if (!existing) {
        return false;
    }

    // Recompile
    auto newShader = loadShader(existing->sourcePath, existing->entryPoint, existing->stage);
    return newShader != nullptr && newShader->isValid;
}

uint32_t ShaderManager::reloadAllChanged() {
    uint32_t reloadCount = 0;

    std::vector<std::string> keys;
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        for (const auto& pair : shaderCache_) {
            keys.push_back(pair.first);
        }
    }

    for (const auto& key : keys) {
        if (reloadIfNeeded(key)) {
            reloadCount++;
        }
    }

    return reloadCount;
}

void ShaderManager::clearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex_);

    for (auto& pair : shaderCache_) {
        if (pair.second->module && device_) {
            device_.destroyShaderModule(pair.second->module);
        }
    }
    shaderCache_.clear();
}

bool ShaderManager::destroyShader(const std::string& key) {
    std::lock_guard<std::mutex> lock(cacheMutex_);

    auto it = shaderCache_.find(key);
    if (it == shaderCache_.end()) {
        return false;
    }

    if (it->second->module && device_) {
        device_.destroyShaderModule(it->second->module);
    }
    shaderCache_.erase(it);
    return true;
}

std::vector<std::string> ShaderManager::getCachedShaderKeys() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);

    std::vector<std::string> keys;
    keys.reserve(shaderCache_.size());
    for (const auto& pair : shaderCache_) {
        keys.push_back(pair.first);
    }
    return keys;
}

} // namespace device
