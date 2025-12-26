# Runtime Shader Compilation with Slang WASM

## Overview

SapphireGem uses the Slang shader language with **WebAssembly-based compilation** for runtime shader processing. This approach provides:

- **Platform Independence**: No OS-specific binaries needed - works on Windows, Linux, and macOS
- **Hot-reloading**: Modify shader files and see changes without recompiling the entire project
- **Sandboxed Execution**: WASM runtime provides isolation and safety
- **Dynamic shader compilation**: Shaders are automatically compiled from .slang source files to SPIR-V
- **Modding Support**: Foundation for future plugin/modding system with secure execution

## Architecture

### WASM Runtime Integration

The engine includes a dedicated `wasm-runtime` subproject that provides:

1. **Wasmtime Integration**: C API wrapper for loading and executing WASM modules
2. **SlangWasmCompiler**: Uses Slang's WASM version for platform-independent shader compilation
3. **Thread Pool Integration**: Uses the engine's `Tasks` class for async shader compilation
4. **Future Extensibility**: Ready for modding/plugin systems with sandboxed code execution

### How It Works

#### Build-Time Setup

1. **Asset Copying**: The CMake build system copies the `assets/` folder to the output directory (`bin/`)
2. **Wasmtime Download**: Automatically downloads Wasmtime runtime (v28.0.0) for the target platform
3. **Slang WASM Download**: Downloads Slang WASM package (latest version or fallback to 2025.24.2)
   - Attempts to fetch the latest release version from GitHub API
   - Platform-independent `.wasm` files instead of native binaries
4. **Library Integration**: Wasmtime and Slang WASM copied to output directory

#### Runtime Compilation

When a material is initialized:

1. The material checks if the shader path ends with `.slang` or `.spv`
2. If it's a `.slang` file:
   - Checks if the corresponding `.spv` file exists
   - If not, or if the `.slang` file is newer, submits compilation task to thread pool
   - SlangWasmCompiler executes Slang WASM to compile shader
   - The compiled `.spv` file is saved alongside the `.slang` source
3. The compiled SPIR-V bytecode is loaded and used to create the Vulkan shader module

### Thread Pool Integration

Shader compilation uses the engine's `Tasks` class for multi-threaded execution:

```cpp
auto& tasks = device::Tasks::get_instance();
auto compileFuture = tasks.add_task([=]() {
    // Compile shader asynchronously
    return compiler.compileShaderToSpirv(slangPath, spvPath);
});
bool success = compileFuture.get(); // Wait for completion
```

## Directory Structure

```
bin/
├── app                    # Main executable
├── libwasmtime.so        # Wasmtime WASM runtime
├── slang-wasm/           # Slang WASM compiler files
│   ├── slang-wasm.js
│   ├── slang-wasm.wasm
│   └── slang-wasm.d.ts
├── assets/
│   ├── shaders/
│   │   ├── shader.slang      # Source shader files
│   │   ├── textured.slang
│   │   ├── textured3d.slang
│   │   ├── layered.slang
│   │   ├── slang.spv         # Compiled shaders (generated at runtime)
│   │   ├── textured.spv
│   │   └── textured3d.spv
│   └── textures/
│       └── ... (PNG files)
```

## Shader Hot-Reload

To modify shaders while the application is running:

1. Edit the `.slang` file in `bin/assets/shaders/`
2. Delete the corresponding `.spv` file (or the engine will detect the source is newer)
3. Reload the material in the application
4. The shader will be automatically recompiled from the updated source via the thread pool

## WASM Runtime for Modding

The `wasm-runtime` subproject provides a foundation for future modding/plugin systems:

```cpp
#include "wasm_runtime.h"

wasm::WasmRuntime runtime;
if (runtime.loadModule("mods/my_mod.wasm")) {
    std::vector<wasmtime_val_t> args, results;
    runtime.callFunction("init", args, results);
}
```

**Benefits for Modding:**
- Sandboxed execution prevents crashes
- Platform-independent mod code
- Safe resource access controls
- Hot-reload capability for development

## Shader Path Configuration

In the codebase, shaders are referenced using relative paths from the binary location:

```cpp
// Before (relative to source directory):
.vertexShaders = "../assets/shaders/textured.spv"

// After (relative to binary directory):
.vertexShaders = "assets/shaders/textured.spv"
```

The Material class automatically handles `.slang` files and compiles them to `.spv` at runtime.

## Slang Shader Language

Slang is a shader language that extends HLSL with modern features:

- Module system for code reuse
- Generics and interfaces
- Automatic differentiation
- Multi-target compilation (SPIR-V, DXIL, Metal, etc.)

Example shader (shader.slang):
```slang
struct VSInput {
    float3 inPosition;
    float3 inColor;
};

struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
ConstantBuffer<UniformBuffer> ubo;

struct VSOutput {
    float4 pos : SV_Position;
    float3 color;
};

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.inPosition, 1.0))));
    output.color = input.inColor;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    return float4(vertIn.color, 1.0);
}
```

## Technical Details

### WASM Runtime Architecture

The `wasm-runtime` subproject (`subprojects/wasm-runtime/`) provides:

**WasmRuntime Class** (`wasm_runtime.h`):
```cpp
class WasmRuntime {
public:
    bool loadModule(const std::filesystem::path& wasmPath);
    bool callFunction(const std::string& functionName,
                     const std::vector<wasmtime_val_t>& args,
                     std::vector<wasmtime_val_t>& results);
    void unloadModule();
};
```

**SlangWasmCompiler Class** (`slang_wasm_compiler.h`):
```cpp
class SlangWasmCompiler {
public:
    bool compileShaderToSpirv(
        const std::filesystem::path& slangFilePath,
        const std::filesystem::path& outputSpvPath,
        const std::vector<std::string>& entryPoints = {"vertMain", "fragMain"}
    );
};
```

### Material Integration

The Material class automatically detects and compiles `.slang` files in the `initialize()` method:

```cpp
// In material.cpp
auto ensureShaderCompiled = [](const std::string& shaderPath) -> std::string {
    // Use WASM-based shader compiler with thread pool
    thread_local wasm::SlangWasmCompiler compiler;
    
    if (path.extension() == ".slang") {
        std::filesystem::path outputPath = path;
        outputPath.replace_extension(".spv");
        
        // Check if needs recompilation
        bool needsCompilation = !std::filesystem::exists(outputPath);
        if (!needsCompilation) {
            auto slangTime = std::filesystem::last_write_time(path);
            auto spvTime = std::filesystem::last_write_time(outputPath);
            needsCompilation = (slangTime > spvTime);
        }
        
        if (needsCompilation) {
            // Compile asynchronously using thread pool
            compiler.compileShaderToSpirv(path, outputPath);
        }
        
        return outputPath.string();
    }
    
    return shaderPath;
};
```

## Platform Support

The build system automatically downloads the appropriate Slang library for:

- **Windows** (x86_64)
- **Linux** (x86_64, aarch64)
- **macOS** (x86_64, aarch64/Apple Silicon)

## Benefits

1. **Faster Iteration**: No need to rebuild the entire project to test shader changes
2. **Better Debugging**: Shader errors are caught at runtime with detailed error messages
3. **Source Control**: Only `.slang` source files need to be tracked in git (`.spv` files can be generated)
4. **Platform Independence**: Shaders are compiled on the target platform ensuring compatibility

## References

- [Slang Language Documentation](https://shader-slang.org/)
- [Slang GitHub Repository](https://github.com/shader-slang/slang)
- [Slang API Reference](https://shader-slang.org/slang/user-guide/compiling.html)

## Platform Support

The WASM runtime approach provides true platform independence:

- **No OS Detection**: Uses Slang WASM package (works everywhere)
- **Wasmtime Runtime**: Downloaded for the specific platform automatically
  - **Windows** (x86_64)
  - **Linux** (x86_64, aarch64)
  - **macOS** (x86_64, aarch64/Apple Silicon)
- **Unified Codebase**: Same shader compiler code runs on all platforms

## Dependencies

### Build-Time
- **Wasmtime C API** (v28.0.0) - automatically downloaded
- **Slang WASM package** (latest or 2025.24.2) - automatically downloaded
- **CMake 3.20+** for JSON parsing and archive extraction

### Runtime
- **Wasmtime** shared library (~2MB)
- **Slang WASM** files (~5MB)
- **Node.js** (optional, for advanced WASM compilation features)

## Benefits

1. **Platform Independence**: WASM runs the same everywhere
2. **Sandboxed Execution**: Safe isolation for shader compilation and mods
3. **Thread-Safe**: Integrated with engine's BS::thread_pool
4. **Hot-Reload**: No rebuild needed for shader changes
5. **Modding Ready**: Foundation for secure plugin system
6. **Automatic Updates**: Latest Slang version auto-detected from GitHub
7. **No Build Dependencies**: No need for platform-specific compiler toolchains

## Future Enhancements

- WASI integration for file I/O in WASM mods
- Resource limits and quotas for mod execution
- Inter-module communication APIs
- Visual scripting integration via WASM
- Hot-reload support for loaded WASM modules
