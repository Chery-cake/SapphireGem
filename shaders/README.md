# Shaders

This directory contains the Slang shader source files for the SapphireGem engine.

## Compiling Shaders

To compile the shaders, run:

```bash
cd shaders
./compile_slang.sh
```

This will compile the shaders using slangc and output them to the `bin/` directory as:
- `slang_vert.spv` - Vertex shader (SPIR-V)
- `slang_frag.spv` - Fragment shader (SPIR-V)

## Shader Files

- `slang.slang` - Main shader source file containing both vertex and fragment shaders

## About Slang

Slang is a shading language that allows you to define multiple shader stages in a single file.
The `slang.slang` file contains both `vertexMain` and `fragmentMain` entry points, which are
compiled separately to their respective SPIR-V modules.
