#pragma once

#include "wasm_runtime.h"
#include <filesystem>
#include <string>
#include <vector>

namespace wasm {

/**
 * @brief Slang shader compiler using WASM version
 * 
 * This class wraps the Slang WASM compiler for compiling .slang shaders
 * to SPIR-V at runtime. It uses the WasmRuntime to execute the WASM version
 * of slangc, providing platform-independent shader compilation.
 */
class SlangWasmCompiler {
public:
    SlangWasmCompiler();
    ~SlangWasmCompiler();

    /**
     * @brief Compile a Slang shader file to SPIR-V
     * @param slangFilePath Path to the .slang source file
     * @param outputSpvPath Path where the .spv file should be written
     * @param entryPoints List of entry point names (e.g., "vertMain", "fragMain")
     * @return true if compilation succeeded
     */
    bool compileShaderToSpirv(
        const std::filesystem::path& slangFilePath,
        const std::filesystem::path& outputSpvPath,
        const std::vector<std::string>& entryPoints = {"vertMain", "fragMain"}
    );

    /**
     * @brief Get the last compilation error message
     */
    const std::string& getLastError() const { return lastError; }

private:
    std::unique_ptr<WasmRuntime> runtime;
    std::string lastError;
    bool initialized;

    bool initialize();
    bool loadSlangWasm();
    std::string generateEntryPointsJson(const std::vector<std::string>& entryPoints);
};

} // namespace wasm
