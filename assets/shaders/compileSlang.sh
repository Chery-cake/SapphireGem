#!/bin/bash

# Compile textured.slang with slangc
# This requires slangc from the Slang shader compiler toolkit
# Download from: https://github.com/shader-slang/slang/releases

export VULKAN_SDK=$HOME/VulkanSDK/1.4.321.1/x86_64
export PATH=$VULKAN_SDK/bin:$PATH
export LD_LIBRARY_PATH=$VULKAN_SDK/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export VK_ADD_LAYER_PATH=$VULKAN_SDK/share/vulkan/explicit_layer.d

# Check if slangc is available
if ! command -v slangc &> /dev/null; then
    echo "Error: slangc not found in PATH"
    echo "Please install Slang compiler from: https://github.com/shader-slang/slang/releases"
    echo "Or update PATH to include slangc"
    exit 1
fi

# Compile shader.slang to textured.spv
slangc shader.slang \
    -target spirv \
    -profile spirv_1_4 \
    -emit-spirv-directly \
    -fvk-use-entrypoint-name \
    -entry vertMain \
    -entry fragMain \
    -o slang.spv

if [ $? -eq 0 ]; then
    echo "Successfully compiled shader.slang to slang.spv"
else
    echo "Error: Failed to compile shader.slang"
    exit 1
fi

# Compile textured.slang to textured.spv
slangc textured.slang \
    -target spirv \
    -profile spirv_1_4 \
    -emit-spirv-directly \
    -fvk-use-entrypoint-name \
    -entry vertMain \
    -entry fragMain \
    -o textured.spv

if [ $? -eq 0 ]; then
    echo "Successfully compiled textured.slang to textured.spv"
else
    echo "Error: Failed to compile textured.slang"
    exit 1
fi

# Compile textured3d.slang to textured3d.spv
slangc textured3d.slang \
    -target spirv \
    -profile spirv_1_4 \
    -emit-spirv-directly \
    -fvk-use-entrypoint-name \
    -entry vertMain \
    -entry fragMain \
    -o textured3d.spv

if [ $? -eq 0 ]; then
    echo "Successfully compiled textured3d.slang to textured3d.spv"
else
    echo "Error: Failed to compile textured3d.slang"
    exit 1
fi
