#include "slang_wasm_compiler.h"
#include "tasks.h"
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <print>
#include <sstream>

namespace wasm {

SlangWasmCompiler::SlangWasmCompiler() : initialized(false) { initialize(); }

SlangWasmCompiler::~SlangWasmCompiler() {}

bool SlangWasmCompiler::initialize() {
  if (initialized) {
    return true;
  }

  // Check if slangc compiler is available
  std::filesystem::path slangcPath =
      std::filesystem::current_path() / "slang-bin" / "slangc";

  if (!std::filesystem::exists(slangcPath)) {
    lastError = "slangc compiler not found at: " + slangcPath.string();
    std::print(stderr, "SlangWasmCompiler: {}\n", lastError);
    return false;
  }

  initialized = true;
  std::print("SlangWasmCompiler: Initialized (using slangc at {})\n",
             slangcPath.string());
  return true;
}

bool SlangWasmCompiler::compileShaderToSpirv(
    const std::filesystem::path &slangFilePath,
    const std::filesystem::path &outputSpvPath,
    const std::vector<std::string> &entryPoints) {

  if (!initialized && !initialize()) {
    lastError = "SlangWasmCompiler not initialized";
    return false;
  }

  if (!std::filesystem::exists(slangFilePath)) {
    lastError = "Shader file does not exist: " + slangFilePath.string();
    std::print(stderr, "SlangWasmCompiler: {}\n", lastError);
    return false;
  }

  std::print("SlangWasmCompiler: Compiling {} to {}\n", slangFilePath.string(),
             outputSpvPath.string());

  // Submit compilation task to thread pool for asynchronous execution
  auto &tasks = device::Tasks::get_instance();

  auto compileFuture = tasks.add_task([=, this]() -> bool {
    // Get absolute path to slangc compiler and lib directory
    std::filesystem::path slangcPath =
        std::filesystem::current_path() / "slang-bin" / "slangc";
    std::filesystem::path slangLibPath =
        std::filesystem::current_path() / "slang-bin" / "lib";

    // Build command line arguments
    std::ostringstream cmdStream;

// Set LD_LIBRARY_PATH on Unix-like systems so slangc can find its shared
// libraries
#ifndef _WIN32
    cmdStream << "LD_LIBRARY_PATH=\"" << slangLibPath.string()
              << ":$LD_LIBRARY_PATH\" ";
#endif

    cmdStream << "\"" << slangcPath.string() << "\" ";
    cmdStream << "-target spirv ";
    cmdStream << "-profile spirv_1_4 ";
    cmdStream << "-emit-spirv-directly ";
    cmdStream << "-fvk-use-entrypoint-name ";

    // Add entry points if specified
    if (!entryPoints.empty()) {
      for (const auto &entry : entryPoints) {
        cmdStream << "-entry " << entry << " ";
      }
    }

    cmdStream << "-o \"" << outputSpvPath.string() << "\" ";
    cmdStream << "\"" << slangFilePath.string() << "\" ";
    cmdStream << "2>&1"; // Capture stderr

    std::string command = cmdStream.str();

    std::print("SlangWasmCompiler: Running: {}\n", command);

    // Execute slangc and capture output
    FILE *pipe = popen(command.c_str(), "r");
    if (!pipe) {
      lastError = "Failed to execute slangc";
      std::print(stderr, "SlangWasmCompiler: {}\n", lastError);
      return false;
    }

    std::array<char, 256> buffer;
    std::string output;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
      output += buffer.data();
    }

    int exitCode = pclose(pipe);

    if (exitCode != 0) {
      lastError = "slangc compilation failed with exit code " +
                  std::to_string(exitCode) + ":\n" + output;
      std::print(stderr, "SlangWasmCompiler: {}\n", lastError);
      return false;
    }

    // Verify output file was created
    if (!std::filesystem::exists(outputSpvPath)) {
      lastError =
          "Output SPIR-V file was not created: " + outputSpvPath.string();
      std::print(stderr, "SlangWasmCompiler: {}\n", lastError);
      return false;
    }

    std::print("SlangWasmCompiler: Compilation successful\n");
    return true;
  });

  // Wait for compilation to complete
  try {
    bool success = compileFuture.get();
    if (!success) {
      return false;
    }
    return true;
  } catch (const std::exception &e) {
    lastError = std::string("Exception during compilation: ") + e.what();
    std::print(stderr, "SlangWasmCompiler: {}\n", lastError);
    return false;
  }
}

std::string SlangWasmCompiler::generateEntryPointsJson(
    const std::vector<std::string> &entryPoints) {
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < entryPoints.size(); ++i) {
    if (i > 0)
      oss << ",";
    oss << "\"" << entryPoints[i] << "\"";
  }
  oss << "]";
  return oss.str();
}

} // namespace wasm
