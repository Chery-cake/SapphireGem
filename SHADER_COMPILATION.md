# Runtime Shader Compilation with Slang

## Overview

SapphireGem now uses the Slang shader language and compiler for runtime shader compilation. This means shaders are compiled when the application starts, not during the build process. This enables:

- **Hot-reloading**: Modify shader files and see changes without recompiling the entire project
- **Dynamic shader compilation**: Shaders are automatically compiled from .slang source files to SPIR-V
- **Cross-platform**: Slang library is automatically downloaded for the target platform (Windows, Linux, macOS)

## How It Works

### Build-Time Setup

1. **Asset Copying**: The CMake build system copies the `assets/` folder to the output directory (`bin/`)
2. **Slang Library Download**: The Slang compiler library is automatically downloaded from GitHub releases
   - Attempts to fetch the latest release version from GitHub API
   - Falls back to a known stable version (currently 2025.24.2) if API is unavailable
   - Downloads the appropriate platform-specific binary (Windows, Linux x86_64/aarch64, macOS)
3. **Library Integration**: The Slang library is copied to the output directory and linked with the graphics module

### Runtime Compilation

When a material is initialized:

1. The material checks if the shader path ends with `.slang` or `.spv`
2. If it's a `.slang` file:
   - Checks if the corresponding `.spv` file exists
   - If not, or if the `.slang` file is newer, compiles the shader using the Slang API
   - The compiled `.spv` file is saved alongside the `.slang` source
3. The compiled SPIR-V bytecode is loaded and used to create the Vulkan shader module

## Directory Structure

```
bin/
├── app                    # Main executable
├── libslang.so           # Slang compiler library
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
4. The shader will be automatically recompiled from the updated source

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

### ShaderCompiler Class

Located in `subprojects/graphics/include/shader_compiler.h`, this class wraps the Slang API:

```cpp
class ShaderCompiler {
public:
    ShaderCompiler();
    ~ShaderCompiler();
    
    bool compileShaderToSpirv(
        const std::filesystem::path& slangFilePath,
        const std::filesystem::path& outputSpvPath,
        const std::vector<std::string>& entryPoints = {"vertMain", "fragMain"}
    );
    
    const std::string& getLastError() const;
};
```

### Material Integration

The Material class automatically detects and compiles `.slang` files in the `initialize()` method:

```cpp
// In material.cpp
auto ensureShaderCompiled = [](const std::string& shaderPath) -> std::string {
    std::filesystem::path path(shaderPath);
    
    // If it's a .slang file, compile it to .spv
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
            static ShaderCompiler compiler;
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
