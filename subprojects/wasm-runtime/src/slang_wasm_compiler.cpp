#include "slang_wasm_compiler.h"
#include "tasks.h"
#include <print>
#include <fstream>
#include <sstream>
#include <cstdlib>

namespace wasm {

SlangWasmCompiler::SlangWasmCompiler() 
    : initialized(false) {
    initialize();
}

SlangWasmCompiler::~SlangWasmCompiler() {
}

bool SlangWasmCompiler::initialize() {
    if (initialized) {
        return true;
    }

    // For Slang WASM, we'll use a Node.js based approach since the WASM
    // version is designed to run in JavaScript environments
    // This is simpler than trying to execute JS from C++ via WASM runtime
    
    initialized = true;
    std::print("SlangWasmCompiler: Initialized (using Node.js bridge)\n");
    return true;
}

bool SlangWasmCompiler::compileShaderToSpirv(
    const std::filesystem::path& slangFilePath,
    const std::filesystem::path& outputSpvPath,
    const std::vector<std::string>& entryPoints
) {
    if (!initialized) {
        lastError = "Compiler not initialized";
        std::print(stderr, "SlangWasmCompiler: {}\n", lastError);
        return false;
    }

    // Check if input file exists
    if (!std::filesystem::exists(slangFilePath)) {
        lastError = "Shader file does not exist: " + slangFilePath.string();
        std::print(stderr, "SlangWasmCompiler: {}\n", lastError);
        return false;
    }

    std::print("SlangWasmCompiler: Compiling {} to {}\n", 
               slangFilePath.string(), outputSpvPath.string());

    // Submit compilation task to thread pool for asynchronous execution
    auto& tasks = device::Tasks::get_instance();
    
    auto compileFuture = tasks.add_task([=]() -> bool {
        // Create a temporary JavaScript file to run the compilation
        std::string jsCode = R"(
const fs = require('fs');
const path = require('path');

// Load Slang WASM module
const slangWasm = require('./slang-wasm/slang-wasm.js');

async function compileShader() {
    try {
        const sourceCode = fs.readFileSync(')" + slangFilePath.string() + R"(', 'utf8');
        
        const result = await slangWasm.compileToSpirv({
            source: sourceCode,
            entryPoints: )" + generateEntryPointsJson(entryPoints) + R"(,
            target: 'spirv_1_4'
        });
        
        if (result.diagnostics) {
            console.error('Compilation diagnostics:', result.diagnostics);
        }
        
        if (result.spirv) {
            fs.writeFileSync(')" + outputSpvPath.string() + R"(', Buffer.from(result.spirv));
            console.log('Compilation successful');
            process.exit(0);
        } else {
            console.error('Compilation failed');
            process.exit(1);
        }
    } catch (error) {
        console.error('Error:', error);
        process.exit(1);
    }
}

compileShader();
)";
        
        // Write temporary JS file
        std::filesystem::path tempJs = std::filesystem::temp_directory_path() / "slang_compile.js";
        std::ofstream jsFile(tempJs);
        if (!jsFile) {
            return false;
        }
        jsFile << jsCode;
        jsFile.close();
        
        // Execute Node.js with the temporary script
        std::string command = "node " + tempJs.string() + " 2>&1";
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) {
            std::filesystem::remove(tempJs);
            return false;
        }
        
        std::string output;
        char buffer[128];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
        }
        
        int exitCode = pclose(pipe);
        std::filesystem::remove(tempJs);
        
        if (exitCode != 0) {
            std::print(stderr, "SlangWasmCompiler: Node.js compilation failed:\n{}\n", output);
            return false;
        }
        
        std::print("SlangWasmCompiler: Successfully compiled {}\n", outputSpvPath.string());
        return true;
    });

    // Wait for compilation to complete
    try {
        bool success = compileFuture.get();
        if (!success) {
            lastError = "Shader compilation failed";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        lastError = std::string("Exception during compilation: ") + e.what();
        std::print(stderr, "SlangWasmCompiler: {}\n", lastError);
        return false;
    }
}

std::string SlangWasmCompiler::generateEntryPointsJson(const std::vector<std::string>& entryPoints) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < entryPoints.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << entryPoints[i] << "\"";
    }
    oss << "]";
    return oss.str();
}

} // namespace wasm
