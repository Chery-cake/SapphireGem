# SapphireGem Usage Examples

This document provides comprehensive examples for using the descriptor pool, synchronization primitives, and buffer creation in the SapphireGem rendering engine.

## Table of Contents
- [Buffer Creation Examples](#buffer-creation-examples)
- [Descriptor Set Usage](#descriptor-set-usage)
- [Rendering System Reload](#rendering-system-reload)

---

## Buffer Creation Examples

### 1. Creating a Vertex Buffer (Static)

Static vertex buffers are ideal for geometry that doesn't change during runtime.

```cpp
#include "buffer_manager.h"
#include "material.h"

// Define vertex data
struct Vertex {
    glm::vec2 position;
    glm::vec3 color;
};

std::vector<Vertex> vertices = {
    {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},  // Red
    {{ 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},  // Green
    {{ 0.0f,  0.5f}, {0.0f, 0.0f, 1.0f}}   // Blue
};

// Create buffer using BufferManager
BufferManager bufferManager(deviceManager);

Buffer::BufferCreateInfo vertexInfo = {
    .identifier = "triangle_vertices",
    .type = Buffer::BufferType::VERTEX,
    .usage = Buffer::BufferUsage::STATIC,
    .size = sizeof(Vertex) * vertices.size(),
    .elementSize = sizeof(Vertex),
    .initialData = vertices.data(),
    .createDescriptorSets = false  // Vertex buffers don't need descriptor sets
};

Buffer* vertexBuffer = bufferManager.create_buffer(vertexInfo);

// Alternative: Use helper method
Buffer* vertexBuffer2 = bufferManager.create_vertex_buffer(
    "triangle_vertices",
    sizeof(Vertex) * vertices.size(),
    vertices.data(),
    Buffer::BufferUsage::STATIC
);
```

### 2. Creating an Index Buffer (Static)

```cpp
// Define index data for indexed drawing
std::vector<uint16_t> indices = {
    0, 1, 2,  // First triangle
    2, 3, 0   // Second triangle (for quad)
};

Buffer::BufferCreateInfo indexInfo = {
    .identifier = "quad_indices",
    .type = Buffer::BufferType::INDEX,
    .usage = Buffer::BufferUsage::STATIC,
    .size = sizeof(uint16_t) * indices.size(),
    .elementSize = sizeof(uint16_t),
    .initialData = indices.data(),
    .createDescriptorSets = false
};

Buffer* indexBuffer = bufferManager.create_buffer(indexInfo);

// Alternative: Use helper method
Buffer* indexBuffer2 = bufferManager.create_index_buffer(
    "quad_indices",
    sizeof(uint16_t) * indices.size(),
    indices.data()
);
```

### 3. Creating a Uniform Buffer (Dynamic) with Descriptor Sets

Uniform buffers that need to be bound to shaders should have descriptor sets enabled.

```cpp
// Define uniform data structure (must match shader layout)
struct CameraUBO {
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec4 position;
};

CameraUBO cameraData = {
    .view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0)),
    .projection = glm::perspective(glm::radians(45.0f), 1920.0f / 1080.0f, 0.1f, 100.0f),
    .position = glm::vec4(0, 0, 5, 1)
};

Buffer::BufferCreateInfo uniformInfo = {
    .identifier = "camera_ubo",
    .type = Buffer::BufferType::UNIFORM,
    .usage = Buffer::BufferUsage::DYNAMIC,
    .size = sizeof(CameraUBO),
    .elementSize = sizeof(CameraUBO),
    .initialData = &cameraData,
    .createDescriptorSets = true  // Enable descriptor sets for shader binding
};

Buffer* cameraBuffer = bufferManager.create_buffer(uniformInfo);

// Update buffer data each frame
void updateCamera(const CameraUBO& newData) {
    cameraBuffer->update_data(&newData, sizeof(CameraUBO));
}
```

### 4. Creating a Material Uniform Buffer with Descriptor Sets

```cpp
// Define material properties structure
struct MaterialProperties {
    glm::vec4 albedoColor;      // Base color
    glm::vec4 emissiveColor;    // Emissive color
    float roughness;             // Surface roughness (0.0 - 1.0)
    float metallic;              // Metallic factor (0.0 - 1.0)
    float ambientOcclusion;      // AO factor
    float padding;               // Alignment padding
};

MaterialProperties materialData = {
    .albedoColor = glm::vec4(0.8f, 0.2f, 0.2f, 1.0f),  // Red material
    .emissiveColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
    .roughness = 0.5f,
    .metallic = 0.0f,
    .ambientOcclusion = 1.0f,
    .padding = 0.0f
};

Buffer::BufferCreateInfo materialUniformInfo = {
    .identifier = "material_properties",
    .type = Buffer::BufferType::UNIFORM,
    .usage = Buffer::BufferUsage::DYNAMIC,
    .size = sizeof(MaterialProperties),
    .elementSize = sizeof(MaterialProperties),
    .initialData = &materialData,
    .createDescriptorSets = true  // Creates descriptor sets for all frames in flight
};

Buffer* materialBuffer = bufferManager.create_buffer(materialUniformInfo);

// The buffer automatically creates descriptor sets (one per frame in flight)
// These are ready to be bound during rendering
```

### 5. Creating a Storage Buffer (Dynamic)

Storage buffers allow read/write access from shaders and are useful for compute operations.

```cpp
// Define particle data for compute shader
struct Particle {
    glm::vec4 position;
    glm::vec4 velocity;
    glm::vec4 color;
    float lifetime;
    float padding[3];
};

std::vector<Particle> particles(1000);  // 1000 particles

// Initialize particles
for (size_t i = 0; i < particles.size(); ++i) {
    particles[i].position = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    particles[i].velocity = glm::vec4(
        (rand() % 200 - 100) / 100.0f,
        (rand() % 200 - 100) / 100.0f,
        (rand() % 200 - 100) / 100.0f,
        0.0f
    );
    particles[i].color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    particles[i].lifetime = 5.0f;
}

Buffer::BufferCreateInfo storageInfo = {
    .identifier = "particle_storage",
    .type = Buffer::BufferType::STORAGE,
    .usage = Buffer::BufferUsage::DYNAMIC,
    .size = sizeof(Particle) * particles.size(),
    .elementSize = sizeof(Particle),
    .initialData = particles.data(),
    .createDescriptorSets = true  // Storage buffers need descriptor sets for compute shaders
};

Buffer* particleBuffer = bufferManager.create_buffer(storageInfo);
```

### 6. Creating Multiple Buffers for a Complex Scene

```cpp
void setupSceneBuffers(BufferManager& bufferManager) {
    // 1. Vertex buffer for mesh
    std::vector<Vertex> meshVertices = loadMeshFromFile("assets/model.obj");
    auto* vertexBuffer = bufferManager.create_vertex_buffer(
        "scene_mesh_vertices",
        sizeof(Vertex) * meshVertices.size(),
        meshVertices.data(),
        Buffer::BufferUsage::STATIC
    );

    // 2. Index buffer for mesh
    std::vector<uint32_t> meshIndices = loadIndicesFromFile("assets/model.obj");
    auto* indexBuffer = bufferManager.create_index_buffer(
        "scene_mesh_indices",
        sizeof(uint32_t) * meshIndices.size(),
        meshIndices.data(),
        Buffer::BufferUsage::STATIC
    );

    // 3. Per-frame camera uniforms
    Buffer::BufferCreateInfo cameraInfo = {
        .identifier = "camera_uniform",
        .type = Buffer::BufferType::UNIFORM,
        .usage = Buffer::BufferUsage::DYNAMIC,
        .size = sizeof(CameraUBO),
        .elementSize = sizeof(CameraUBO),
        .initialData = nullptr,  // Will be updated each frame
        .createDescriptorSets = true
    };
    auto* cameraBuffer = bufferManager.create_buffer(cameraInfo);

    // 4. Instance data buffer for multiple objects
    struct InstanceData {
        glm::mat4 modelMatrix;
        glm::vec4 color;
    };
    
    std::vector<InstanceData> instances(100);  // 100 instances
    for (size_t i = 0; i < instances.size(); ++i) {
        instances[i].modelMatrix = glm::translate(
            glm::mat4(1.0f),
            glm::vec3((i % 10) * 2.0f, 0.0f, (i / 10) * 2.0f)
        );
        instances[i].color = glm::vec4(
            (i % 10) / 10.0f,
            (i / 10) / 10.0f,
            0.5f,
            1.0f
        );
    }

    Buffer::BufferCreateInfo instanceInfo = {
        .identifier = "instance_data",
        .type = Buffer::BufferType::STORAGE,
        .usage = Buffer::BufferUsage::DYNAMIC,
        .size = sizeof(InstanceData) * instances.size(),
        .elementSize = sizeof(InstanceData),
        .initialData = instances.data(),
        .createDescriptorSets = true
    };
    auto* instanceBuffer = bufferManager.create_buffer(instanceInfo);

    // 5. Material properties buffer
    Buffer::BufferCreateInfo materialInfo = {
        .identifier = "material_properties",
        .type = Buffer::BufferType::UNIFORM,
        .usage = Buffer::BufferUsage::DYNAMIC,
        .size = sizeof(MaterialProperties),
        .elementSize = sizeof(MaterialProperties),
        .initialData = nullptr,
        .createDescriptorSets = true
    };
    auto* materialBuffer = bufferManager.create_buffer(materialInfo);
}
```

---

## Descriptor Set Usage

### Using Material Descriptor Sets in Rendering

```cpp
void renderFrame(uint32_t currentFrame) {
    uint32_t deviceIndex = 0;  // Primary device
    
    // Get material
    Material* material = materialManager->get_material("pbr_material");
    
    // Bind material with frame-specific descriptor set
    vk::raii::CommandBuffer& cmd = commandBuffers[currentFrame];
    cmd.begin(beginInfo);
    
    // Bind the material - this automatically binds the descriptor set for currentFrame
    material->bind(cmd, deviceIndex, currentFrame);
    
    // The descriptor set for this frame is now bound and ready to use
    // Draw calls can now access the uniform buffers via the descriptor set
    cmd.draw(vertexCount, 1, 0, 0);
    
    cmd.end();
}
```

### Accessing Buffer Descriptor Sets Directly

```cpp
// If you need to manually work with buffer descriptor sets
Buffer* uniformBuffer = bufferManager->get_buffer("camera_ubo");

// The buffer has descriptor sets created automatically (if createDescriptorSets was true)
// These are managed internally and bound as needed
// You typically don't need to access them directly
```

---

## Rendering System Reload

### Full Reload (Instance + Devices + Resources)

A full reload is required when instance-level configuration changes (layers, extensions).

```cpp
void performFullReload() {
    // Example: Enable/disable validation layers
    if (shouldEnableValidation) {
        Config::get_instance().add_instance_layer("VK_LAYER_KHRONOS_validation");
    } else {
        Config::get_instance().remove_instance_layer("VK_LAYER_KHRONOS_validation");
    }
    
    // This flags that a full reload is needed
    // The reload() method will detect this via needs_reload()
    
    renderer->reload();
    
    // After reload:
    // - Vulkan instance is recreated
    // - Debug messenger is recreated
    // - Surface is recreated
    // - All devices are recreated
    // - Swap chains are recreated
    // - All materials are recreated
    // - All buffers are recreated
}
```

**Full Reload Flow:**
```
1. Wait for all devices to idle
2. Destroy buffers → materials → devices
3. Destroy debug messenger → surface → instance
4. Recreate instance with new configuration
5. Recreate debug messenger and surface
6. Recreate devices and query capabilities
7. Recreate swap chains
8. Recreate materials with new pipeline layouts
9. Recreate all buffers
```

### Partial Reload (Devices + Resources Only)

A partial reload keeps the instance but recreates devices and downstream resources. This is faster and used for device-level changes.

```cpp
void performPartialReload() {
    // Example: Change max frames in flight
    uint8_t newMaxFrames = 3;
    Config::get_instance().set_max_frames(newMaxFrames);
    
    // This does NOT trigger needs_reload() since it's not an instance-level change
    
    renderer->reload();
    
    // After reload:
    // - Vulkan instance is kept
    // - Debug messenger is kept
    // - Surface is kept
    // - All devices are recreated
    // - Swap chains are recreated
    // - All materials are recreated with new descriptor set counts
    // - All buffers are recreated with new descriptor set counts
}
```

**Partial Reload Flow:**
```
1. Wait for all devices to idle
2. Destroy buffers → materials → devices
3. Keep instance, debug messenger, and surface
4. Recreate devices
5. Recreate swap chains
6. Recreate materials
7. Recreate buffers
```

### Reload Example with Device Configuration Changes

```cpp
void changeDeviceExtensions() {
    // Add optional device extension
    Config::get_instance().add_optional_device_extension("VK_EXT_descriptor_indexing");
    
    // Perform partial reload
    renderer->reload();
    
    // The new extension will be checked and enabled if available
    // All device resources are recreated with the new configuration
}
```

### Reload Example with Swap Chain Recreation

```cpp
void handleWindowResize(int newWidth, int newHeight) {
    // Wait for device to be idle
    renderer->get_device_manager().wait_idle();
    
    // Destroy and recreate swap chain-dependent resources
    // This is lighter than a full reload
    renderer->get_device_manager().recreate_swap_chains();
    
    // Materials don't need to be recreated for simple swap chain recreation
    // Only framebuffers and render passes (if using traditional rendering) need updates
}
```

### Complete Reload Example with State Preservation

```cpp
class Application {
private:
    std::unique_ptr<Renderer> renderer;
    
    // Application state to preserve across reloads
    struct AppState {
        std::vector<Vertex> sceneVertices;
        std::vector<uint32_t> sceneIndices;
        CameraUBO cameraData;
        std::vector<MaterialProperties> materials;
    } state;
    
public:
    void reloadWithStatePreservation() {
        // Save current state before reload
        saveCurrentState();
        
        // Perform reload
        renderer->reload();
        
        // Restore state after reload
        restoreState();
    }
    
    void saveCurrentState() {
        // Extract data from buffers if needed
        // (In this example, we already have it in 'state')
        std::print("Saving application state before reload...\n");
    }
    
    void restoreState() {
        std::print("Restoring application state after reload...\n");
        
        BufferManager* bufferManager = /* get buffer manager */;
        
        // Recreate vertex buffer
        bufferManager->create_vertex_buffer(
            "scene_vertices",
            sizeof(Vertex) * state.sceneVertices.size(),
            state.sceneVertices.data()
        );
        
        // Recreate index buffer
        bufferManager->create_index_buffer(
            "scene_indices",
            sizeof(uint32_t) * state.sceneIndices.size(),
            state.sceneIndices.data()
        );
        
        // Recreate camera buffer
        Buffer::BufferCreateInfo cameraInfo = {
            .identifier = "camera_ubo",
            .type = Buffer::BufferType::UNIFORM,
            .usage = Buffer::BufferUsage::DYNAMIC,
            .size = sizeof(CameraUBO),
            .initialData = &state.cameraData,
            .createDescriptorSets = true
        };
        bufferManager->create_buffer(cameraInfo);
        
        // Recreate material buffers
        for (size_t i = 0; i < state.materials.size(); ++i) {
            Buffer::BufferCreateInfo matInfo = {
                .identifier = "material_" + std::to_string(i),
                .type = Buffer::BufferType::UNIFORM,
                .usage = Buffer::BufferUsage::DYNAMIC,
                .size = sizeof(MaterialProperties),
                .initialData = &state.materials[i],
                .createDescriptorSets = true
            };
            bufferManager->create_buffer(matInfo);
        }
    }
};
```

### Runtime Reload Trigger Example

```cpp
void checkForReloadTriggers() {
    // Example: Reload on key press
    if (glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS) {
        std::print("F5 pressed - triggering partial reload...\n");
        renderer->reload();
    }
    
    // Example: Reload when configuration file changes
    if (configFileChanged()) {
        std::print("Configuration changed - triggering reload...\n");
        reloadConfiguration();
        renderer->reload();
    }
    
    // Example: Automatic reload on validation errors (debug mode)
    #ifdef DEBUG_MODE
    if (validationErrorDetected) {
        std::print("Validation error detected - triggering reload...\n");
        renderer->reload();
        validationErrorDetected = false;
    }
    #endif
}
```

---

## Synchronization Primitives Usage

### Using Frame Synchronization

```cpp
void renderLoop() {
    uint32_t currentFrame = 0;
    const uint8_t maxFrames = Config::get_instance().get_max_frames();
    
    while (!glfwWindowShouldClose(window)) {
        // Wait for the fence of the current frame
        LogicalDevice* device = deviceManager->get_primary_device();
        const auto& fence = device->get_in_flight_fence(currentFrame);
        
        // Wait for this frame to be ready
        vk::Result result = device->get_device().waitForFences(
            *fence,
            VK_TRUE,
            UINT64_MAX
        );
        
        // Reset the fence for this frame
        device->get_device().resetFences(*fence);
        
        // Acquire next swap chain image
        uint32_t imageIndex;
        const auto& imageAvailableSem = device->get_image_available_semaphore(currentFrame);
        
        result = device->get_swap_chain().acquire_next_image(
            UINT64_MAX,
            *imageAvailableSem,
            nullptr,
            imageIndex
        );
        
        // Record and submit commands
        recordCommandBuffer(currentFrame, imageIndex);
        
        const auto& renderFinishedSem = device->get_render_finished_semaphore(currentFrame);
        
        vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        vk::SubmitInfo submitInfo{
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*imageAvailableSem,
            .pWaitDstStageMask = &waitStage,
            .commandBufferCount = 1,
            .pCommandBuffers = &*commandBuffers[currentFrame],
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &*renderFinishedSem
        };
        
        device->get_graphics_queue().submit(submitInfo, *fence);
        
        // Present
        vk::PresentInfoKHR presentInfo{
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*renderFinishedSem,
            .swapchainCount = 1,
            .pSwapchains = &*swapChain,
            .pImageIndices = &imageIndex
        };
        
        device->get_graphics_queue().presentKHR(presentInfo);
        
        // Move to next frame
        currentFrame = (currentFrame + 1) % maxFrames;
    }
    
    // Wait for all operations to complete before cleanup
    device->wait_idle();
}
```

---

## Best Practices

1. **Buffer Creation**
   - Use `STATIC` usage for data that never changes (geometry)
   - Use `DYNAMIC` usage for data that updates frequently (uniforms, particles)
   - Only enable `createDescriptorSets` for UNIFORM and STORAGE buffers that need shader binding
   - Always specify `elementSize` for buffers with multiple elements

2. **Descriptor Sets**
   - Descriptor sets are automatically created per frame in flight
   - Materials automatically manage descriptor sets when bindings are specified
   - Always pass the correct `frameIndex` when binding materials

3. **Synchronization**
   - Always wait for fences before reusing command buffers
   - Use semaphores to synchronize GPU operations
   - Use fences to synchronize CPU-GPU operations

4. **Reload Operations**
   - Save application state before reload
   - Use partial reload when possible (faster than full reload)
   - Full reload is only needed for instance-level changes
   - Always wait for device idle before reload

5. **Multi-GPU Support**
   - Specify `deviceIndex` when binding materials or buffers
   - Each device has its own descriptor pool and synchronization primitives
   - Resources are automatically created for all devices
