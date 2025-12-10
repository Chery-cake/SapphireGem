#!/bin/bash

# Compile vertex shader
glslangValidator -V shader.vert -o ../bin/slang_vert.spv

# Compile fragment shader
glslangValidator -V shader.frag -o ../bin/slang_frag.spv

echo "Shaders compiled successfully"
