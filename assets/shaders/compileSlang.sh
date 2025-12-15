#!/bin/bash

export VULKAN_SDK=$HOME/VulkanSDK/1.4.321.1/x86_64
export PATH=$VULKAN_SDK/bin:$PATH
export LD_LIBRARY_PATH=$VULKAN_SDK/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export VK_ADD_LAYER_PATH=$VULKAN_SDK/share/vulkan/explicit_layer.d

/home/chery/VulkanSDK/1.4.321.1/x86_64/bin/slangc shader.slang -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertMain -entry fragMain -o slang.spv
