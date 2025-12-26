#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <wasm.h>
#include <wasmtime.h>

namespace wasm {

/**
 * @brief WASM Runtime manager using Wasmtime
 *
 * This class provides a high-level interface for loading and executing
 * WebAssembly modules. It's designed to be used for:
 * - Shader compilation (Slang WASM)
 * - Modding support (sandboxed user code)
 * - Plugin systems
 */
class WasmRuntime {
public:
  WasmRuntime();
  ~WasmRuntime();

  // Delete copy constructor and assignment
  WasmRuntime(const WasmRuntime &) = delete;
  WasmRuntime &operator=(const WasmRuntime &) = delete;

  /**
   * @brief Load a WASM module from file
   * @param wasmPath Path to the .wasm file
   * @return true if module loaded successfully
   */
  bool loadModule(const std::filesystem::path &wasmPath);

  /**
   * @brief Check if a module is currently loaded
   */
  bool isModuleLoaded() const { return moduleLoaded; }

  /**
   * @brief Unload the current module
   */
  void unloadModule();

  /**
   * @brief Call an exported function from the loaded WASM module
   * @param functionName Name of the exported function
   * @param args Arguments to pass to the function
   * @param results Output results from the function
   * @return true if call succeeded
   */
  bool callFunction(const std::string &functionName,
                    const std::vector<wasmtime_val_t> &args,
                    std::vector<wasmtime_val_t> &results);

  /**
   * @brief Get the last error message
   */
  const std::string &getLastError() const { return lastError; }

private:
  wasm_engine_t *engine;
  wasmtime_store_t *store;
  wasmtime_context_t *context;
  wasmtime_module_t *module;
  wasmtime_instance_t instance;

  bool engineInitialized;
  bool moduleLoaded;
  std::string lastError;

  bool initializeEngine();
  void shutdownEngine();
  void setError(const std::string &error);
};

} // namespace wasm
