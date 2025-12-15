# Textured Shader Compilation

## Overview
The `textured.slang` shader file contains both vertex and fragment shaders in Slang format.
It needs to be compiled with `slangc` to create a single `textured.spv` SPIR-V file.

## Compilation

Run the compilation script:
```bash
./compileTexturedSlang.sh
```

Or manually:
```bash
slangc textured.slang -target spirv -profile spirv_1_4 -emit-spirv-directly \
       -fvk-use-entrypoint-name -entry vertMain -entry fragMain -o textured.spv
```

## Requirements
- Slang Compiler (`slangc`): Download from https://github.com/shader-slang/slang/releases
- Vulkan SDK (for SPIR-V tools)

## Entry Points
- Vertex shader: `vertMain`
- Fragment shader: `fragMain`

## Current Status
⚠️ **Note**: The current `textured.spv` is a placeholder copied from `slang.spv`.
For proper textured rendering, recompile with slangc using the command above.
