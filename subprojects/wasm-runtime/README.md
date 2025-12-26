# WASM Runtime Subproject

## Overview

This subproject provides WebAssembly runtime support for the SapphireGem engine, enabling:

1. **Platform-independent shader compilation** using Slang WASM
2. **Modding support** with sandboxed code execution
3. **Plugin systems** for future extensibility

## Architecture

### Components

- **WasmRuntime**: Core WASM execution engine using Wasmtime
  - Load and execute WASM modules
  - Call exported functions
  - Sandboxed execution environment

- **SlangWasmCompiler**: Shader compilation using Slang WASM
  - Compiles `.slang` shaders to SPIR-V at runtime
  - Uses thread pool from Tasks class for async compilation
  - Platform-independent (no OS-specific binaries needed)

## Usage

### Shader Compilation

```cpp
#include "slang_wasm_compiler.h"

wasm::SlangWasmCompiler compiler;
bool success = compiler.compileShaderToSpirv(
    "assets/shaders/my_shader.slang",
    "assets/shaders/my_shader.spv",
    {"vertMain", "fragMain"}
);
```

### Loading WASM Modules (for mods/plugins)

```cpp
#include "wasm_runtime.h"

wasm::WasmRuntime runtime;
if (runtime.loadModule("mods/my_mod.wasm")) {
    // Call exported functions
    std::vector<wasmtime_val_t> args, results;
    runtime.callFunction("init", args, results);
}
```

## Thread Safety

The WASM runtime integrates with the engine's thread pool (Tasks class) for:
- Asynchronous shader compilation
- Parallel mod execution
- Non-blocking operations

## Dependencies

- **Wasmtime**: C API for WASM execution (automatically downloaded)
- **Slang WASM**: Platform-independent shader compiler (automatically downloaded)
- **BS::thread_pool**: Thread pool from common module

## Future Enhancements

- Resource limits for mod execution
- Hot-reload support for WASM modules
- Inter-module communication
- WASI support for file I/O in mods
