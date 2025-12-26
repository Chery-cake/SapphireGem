#include "wasm_runtime.h"
#include <cstring>
#include <fstream>
#include <print>

wasm::WasmRuntime::WasmRuntime()
    : engine(nullptr), store(nullptr), context(nullptr), module(nullptr),
      engineInitialized(false), moduleLoaded(false) {
  initializeEngine();
}

wasm::WasmRuntime::~WasmRuntime() {
  unloadModule();
  shutdownEngine();
}

bool wasm::WasmRuntime::initializeEngine() {
  // Create WASM engine
  engine = wasm_engine_new();
  if (!engine) {
    setError("Failed to create Wasmtime engine");
    return false;
  }

  // Create store
  store = wasmtime_store_new(engine, nullptr, nullptr);
  if (!store) {
    setError("Failed to create Wasmtime store");
    wasm_engine_delete(engine);
    engine = nullptr;
    return false;
  }

  context = wasmtime_store_context(store);
  engineInitialized = true;

  std::print("WasmRuntime: Wasmtime engine initialized successfully\n");
  return true;
}

void wasm::WasmRuntime::shutdownEngine() {
  if (store) {
    wasmtime_store_delete(store);
    store = nullptr;
    context = nullptr;
  }
  if (engine) {
    wasm_engine_delete(engine);
    engine = nullptr;
  }
  engineInitialized = false;
}

bool wasm::WasmRuntime::loadModule(const std::filesystem::path &wasmPath) {
  if (!engineInitialized) {
    setError("WASM engine not initialized");
    return false;
  }

  // Check if file exists
  if (!std::filesystem::exists(wasmPath)) {
    setError("WASM file does not exist: " + wasmPath.string());
    return false;
  }

  // Read WASM file
  std::ifstream file(wasmPath, std::ios::binary | std::ios::ate);
  if (!file) {
    setError("Failed to open WASM file: " + wasmPath.string());
    return false;
  }

  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<uint8_t> buffer(size);
  if (!file.read(reinterpret_cast<char *>(buffer.data()), size)) {
    setError("Failed to read WASM file: " + wasmPath.string());
    return false;
  }

  // Create WASM byte vector
  wasm_byte_vec_t wasm_bytes;
  wasm_byte_vec_new_uninitialized(&wasm_bytes, buffer.size());
  std::memcpy(wasm_bytes.data, buffer.data(), buffer.size());

  // Compile module
  wasmtime_error_t *error = wasmtime_module_new(
      engine, (uint8_t *)wasm_bytes.data, wasm_bytes.size, &module);
  wasm_byte_vec_delete(&wasm_bytes);

  if (error) {
    wasm_name_t message;
    wasmtime_error_message(error, &message);
    setError(std::string("Failed to compile WASM module: ") +
             std::string(message.data, message.size));
    wasm_byte_vec_delete(&message);
    wasmtime_error_delete(error);
    return false;
  }

  // Instantiate module
  wasm_trap_t *trap = nullptr;
  error = wasmtime_instance_new(context, module, nullptr, 0, &instance, &trap);

  if (error || trap) {
    if (error) {
      wasm_name_t message;
      wasmtime_error_message(error, &message);
      setError(std::string("Failed to instantiate module: ") +
               std::string(message.data, message.size));
      wasm_byte_vec_delete(&message);
      wasmtime_error_delete(error);
    }
    if (trap) {
      wasm_message_t message;
      wasm_trap_message(trap, &message);
      setError(std::string("Trap during instantiation: ") +
               std::string(message.data, message.size));
      wasm_byte_vec_delete(&message);
      wasm_trap_delete(trap);
    }
    wasmtime_module_delete(module);
    module = nullptr;
    return false;
  }

  moduleLoaded = true;
  std::print("WasmRuntime: Module loaded successfully from {}\n",
             wasmPath.string());
  return true;
}

void wasm::WasmRuntime::unloadModule() {
  if (module) {
    wasmtime_module_delete(module);
    module = nullptr;
  }
  moduleLoaded = false;
}

bool wasm::WasmRuntime::callFunction(const std::string &functionName,
                                     const std::vector<wasmtime_val_t> &args,
                                     std::vector<wasmtime_val_t> &results) {
  if (!moduleLoaded) {
    setError("No module loaded");
    return false;
  }

  // Get the function export
  wasmtime_extern_t func_extern;
  if (!wasmtime_instance_export_get(context, &instance, functionName.c_str(),
                                    functionName.size(), &func_extern)) {
    setError("Function not found: " + functionName);
    return false;
  }

  if (func_extern.kind != WASMTIME_EXTERN_FUNC) {
    setError("Export is not a function: " + functionName);
    return false;
  }

  // Call the function
  wasm_trap_t *trap = nullptr;
  wasmtime_val_t *results_ptr = results.empty() ? nullptr : results.data();
  wasmtime_error_t *error =
      wasmtime_func_call(context, &func_extern.of.func, args.data(),
                         args.size(), results_ptr, results.size(), &trap);

  if (error || trap) {
    if (error) {
      wasm_name_t message;
      wasmtime_error_message(error, &message);
      setError(std::string("Error calling function: ") +
               std::string(message.data, message.size));
      wasm_byte_vec_delete(&message);
      wasmtime_error_delete(error);
    }
    if (trap) {
      wasm_message_t message;
      wasm_trap_message(trap, &message);
      setError(std::string("Trap during function call: ") +
               std::string(message.data, message.size));
      wasm_byte_vec_delete(&message);
      wasm_trap_delete(trap);
    }
    return false;
  }

  return true;
}

void wasm::WasmRuntime::setError(const std::string &error) {
  lastError = error;
  std::print(stderr, "WasmRuntime: {}\n", error);
}
