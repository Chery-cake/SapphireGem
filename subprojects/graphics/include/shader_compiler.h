#pragma once

#include <slang.h>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>

namespace render {

class ShaderCompiler {
public:
    ShaderCompiler();
    ~ShaderCompiler();

    // Compile a Slang shader file to SPIR-V
    // Returns true on success, false on failure
    bool compileShaderToSpirv(
        const std::filesystem::path& slangFilePath,
        const std::filesystem::path& outputSpvPath,
        const std::vector<std::string>& entryPoints = {"vertMain", "fragMain"}
    );

    // Get the last compilation error message
    const std::string& getLastError() const { return lastError; }

private:
    SlangSession* slangSession;
    std::string lastError;

    bool initializeSlang();
    void shutdownSlang();
};

} // namespace render
