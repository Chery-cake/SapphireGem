#!/bin/bash

# Compile Slang shader to SPIR-V
# Using slangc, we compile both vertex and fragment shaders from a single source file

mkdir -p ../bin

slangc slang.slang -target spirv -stage vertex -entry vertexMain -o ../bin/slang_vert.spv
slangc slang.slang -target spirv -stage fragment -entry fragmentMain -o ../bin/slang_frag.spv

echo "Slang shaders compiled successfully"
echo "Both shaders compiled from single slang.slang source file"
