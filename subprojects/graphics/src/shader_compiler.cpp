#include "shader_compiler.h"
#include <print>
#include <fstream>
#include <unordered_map>

namespace render {

ShaderCompiler::ShaderCompiler() : slangSession(nullptr) {
    initializeSlang();
}

ShaderCompiler::~ShaderCompiler() {
    shutdownSlang();
}

bool ShaderCompiler::initializeSlang() {
    // Create a global session for Slang
    slangSession = spCreateSession(nullptr);
    if (!slangSession) {
        lastError = "Failed to create Slang session";
        std::print(stderr, "ShaderCompiler: {}\n", lastError);
        return false;
    }

    std::print("ShaderCompiler: Slang session initialized successfully\n");
    return true;
}

void ShaderCompiler::shutdownSlang() {
    if (slangSession) {
        spDestroySession(slangSession);
        slangSession = nullptr;
    }
}

bool ShaderCompiler::compileShaderToSpirv(
    const std::filesystem::path& slangFilePath,
    const std::filesystem::path& outputSpvPath,
    const std::vector<std::string>& entryPoints
) {
    if (!slangSession) {
        lastError = "Slang session not initialized";
        std::print(stderr, "ShaderCompiler: {}\n", lastError);
        return false;
    }

    // Check if input file exists
    if (!std::filesystem::exists(slangFilePath)) {
        lastError = "Shader file does not exist: " + slangFilePath.string();
        std::print(stderr, "ShaderCompiler: {}\n", lastError);
        return false;
    }

    std::print("ShaderCompiler: Compiling {} to {}\n", 
               slangFilePath.string(), outputSpvPath.string());

    // Set up compilation target
    SlangCompileRequest* request = spCreateCompileRequest(slangSession);
    if (!request) {
        lastError = "Failed to create compile request";
        std::print(stderr, "ShaderCompiler: {}\n", lastError);
        return false;
    }

    // Configure the compilation target for SPIR-V
    int targetIndex = spAddCodeGenTarget(request, SLANG_SPIRV);
    spSetTargetProfile(request, targetIndex, spFindProfile(slangSession, "spirv_1_4"));
    spSetTargetFlags(request, targetIndex, SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY);

    // Add the shader file as a translation unit
    int translationUnitIndex = spAddTranslationUnit(request, SLANG_SOURCE_LANGUAGE_SLANG, nullptr);
    spAddTranslationUnitSourceFile(request, translationUnitIndex, slangFilePath.string().c_str());

    // Entry point name to stage mapping
    static const std::unordered_map<std::string, SlangStage> stageMap = {
        {"vertMain", SLANG_STAGE_VERTEX},
        {"fragMain", SLANG_STAGE_FRAGMENT},
        {"computeMain", SLANG_STAGE_COMPUTE},
        {"geomMain", SLANG_STAGE_GEOMETRY},
        {"hullMain", SLANG_STAGE_HULL},
        {"domainMain", SLANG_STAGE_DOMAIN},
    };

    // Add entry points with appropriate stages
    for (const auto& entryPoint : entryPoints) {
        auto it = stageMap.find(entryPoint);
        SlangStage stage = (it != stageMap.end()) ? it->second : SLANG_STAGE_FRAGMENT;
        spAddEntryPoint(request, translationUnitIndex, entryPoint.c_str(), stage);
    }

    // Compile the shader
    const SlangResult compileResult = spCompile(request);
    
    // Check for errors
    if (SLANG_FAILED(compileResult)) {
        const char* diagnostics = spGetDiagnosticOutput(request);
        lastError = std::string("Shader compilation failed:\n") + 
                   (diagnostics ? diagnostics : "Unknown error");
        std::print(stderr, "ShaderCompiler: {}\n", lastError);
        spDestroyCompileRequest(request);
        return false;
    }

    // Get the compiled SPIR-V code
    // Note: For multi-entry-point shaders, Slang combines them into a single SPIR-V module
    // We use the target code output which includes all entry points
    size_t codeSize = 0;
    const void* code = spGetEntryPointCode(request, 0, &codeSize);
    
    if (!code || codeSize == 0) {
        lastError = "Failed to get compiled SPIR-V code";
        std::print(stderr, "ShaderCompiler: {}\n", lastError);
        spDestroyCompileRequest(request);
        return false;
    }

    // Create output directory if it doesn't exist
    std::filesystem::create_directories(outputSpvPath.parent_path());

    // Write SPIR-V to file
    std::ofstream outFile(outputSpvPath, std::ios::binary);
    if (!outFile) {
        lastError = "Failed to open output file: " + outputSpvPath.string();
        std::print(stderr, "ShaderCompiler: {}\n", lastError);
        spDestroyCompileRequest(request);
        return false;
    }

    outFile.write(static_cast<const char*>(code), codeSize);
    outFile.close();

    if (!outFile.good()) {
        lastError = "Failed to write SPIR-V to file: " + outputSpvPath.string();
        std::print(stderr, "ShaderCompiler: {}\n", lastError);
        spDestroyCompileRequest(request);
        return false;
    }

    std::print("ShaderCompiler: Successfully compiled {} ({} bytes)\n", 
               outputSpvPath.string(), codeSize);

    // Clean up
    spDestroyCompileRequest(request);
    return true;
}

} // namespace render
