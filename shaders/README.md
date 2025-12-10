# Shaders

This directory contains the GLSL shader source files for the SapphireGem engine.

## Compiling Shaders

To compile the shaders, run:

```bash
cd shaders
./compile_shaders.sh
```

This will compile the shaders using glslangValidator and output them to the `bin/` directory as:
- `slang_vert.spv` - Vertex shader
- `slang_frag.spv` - Fragment shader

## Shader Files

- `shader.vert` - Vertex shader source
- `shader.frag` - Fragment shader source
