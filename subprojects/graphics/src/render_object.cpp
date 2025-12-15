#include "render_object.h"
#include "buffer_manager.h"
#include "material.h"
#include "material_manager.h"
#include "texture_manager.h"
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <print>
#include <vulkan/vulkan_raii.hpp>

RenderObject::RenderObject(const ObjectCreateInfo createInfo,
                           BufferManager *bufferManager,
                           MaterialManager *materialManager,
                           TextureManager *textureManager)
    : identifier(createInfo.identifier), type(createInfo.type),
      indexCount(createInfo.indices.size()),
      materialIdentifier(createInfo.materialIdentifier),
      position(createInfo.position), rotation(createInfo.rotation),
      scale(createInfo.scale), transformDirty(true),
      visible(createInfo.visible), bufferManager(bufferManager),
      materialManager(materialManager), textureManager(textureManager),
      originalVertices(createInfo.vertices),
      transformedVertices(createInfo.vertices), verticesDirty(true),
      transformMode(TransformMode::CPU_VERTICES) {
  // Create unique buffer names for this object
  vertexBufferName = identifier + "_vertices";
  indexBufferName = identifier + "_indices";

  // Create vertex buffer
  Buffer::BufferCreateInfo vertInfo = {
      .identifier = vertexBufferName,
      .type = Buffer::BufferType::VERTEX,
      .usage = Buffer::BufferUsage::DYNAMIC,
      .size = createInfo.vertices.size() * sizeof(Material::Vertex2D),
      .elementSize = sizeof(Material::Vertex2D),
      .initialData = createInfo.vertices.data()};

  bufferManager->create_buffer(vertInfo);

  // Create index buffer
  Buffer::BufferCreateInfo indInfo = {.identifier = indexBufferName,
                                      .type = Buffer::BufferType::INDEX,
                                      .usage = Buffer::BufferUsage::STATIC,
                                      .size = createInfo.indices.size() *
                                              sizeof(uint16_t),
                                      .initialData = createInfo.indices.data()};

  bufferManager->create_buffer(indInfo);

  // Get material reference
  auto materials = materialManager->get_materials();
  material = nullptr;
  for (auto *mat : materials) {
    if (mat->get_identifier() == materialIdentifier) {
      material = mat;
      break;
    }
  }

  if (!material) {
    std::print("Warning: Material '{}' not found for object '{}'\n",
               materialIdentifier, identifier);
  }

  update_model_matrix();
  update_vertices();
}

RenderObject::RenderObject(const ObjectCreateInfoTextured createInfo,
                           BufferManager *bufferManager,
                           MaterialManager *materialManager,
                           TextureManager *textureManager)
    : identifier(createInfo.identifier), type(createInfo.type),
      indexCount(createInfo.indices.size()),
      materialIdentifier(createInfo.materialIdentifier),
      textureIdentifier(createInfo.textureIdentifier),
      position(createInfo.position), rotation(createInfo.rotation),
      scale(createInfo.scale), transformDirty(true),
      visible(createInfo.visible), bufferManager(bufferManager),
      materialManager(materialManager), textureManager(textureManager),
      originalVertices(), transformedVertices(), verticesDirty(false),
      transformMode(TransformMode::GPU_MATRIX) {
  // Create unique buffer names for this object
  vertexBufferName = identifier + "_vertices";
  indexBufferName = identifier + "_indices";

  // Create vertex buffer for textured vertices
  // Use DYNAMIC for textured objects since they don't use vertex transformation
  // but the buffer might need updates in the future
  Buffer::BufferCreateInfo vertInfo = {
      .identifier = vertexBufferName,
      .type = Buffer::BufferType::VERTEX,
      .usage = Buffer::BufferUsage::DYNAMIC,
      .size = createInfo.vertices.size() * sizeof(Material::Vertex2DTextured),
      .elementSize = sizeof(Material::Vertex2DTextured),
      .initialData = createInfo.vertices.data()};

  bufferManager->create_buffer(vertInfo);

  // Create index buffer
  Buffer::BufferCreateInfo indInfo = {.identifier = indexBufferName,
                                      .type = Buffer::BufferType::INDEX,
                                      .usage = Buffer::BufferUsage::STATIC,
                                      .size = createInfo.indices.size() *
                                              sizeof(uint16_t),
                                      .initialData = createInfo.indices.data()};

  bufferManager->create_buffer(indInfo);

  // Get material reference
  auto materials = materialManager->get_materials();
  material = nullptr;
  for (auto *mat : materials) {
    if (mat->get_identifier() == materialIdentifier) {
      material = mat;
      break;
    }
  }

  if (!material) {
    std::print("Warning: Material '{}' not found for object '{}'\n",
               materialIdentifier, identifier);
  }

  update_model_matrix();
  // Note: Textured objects don't use vertex transformation
}

RenderObject::~RenderObject() {
  if (bufferManager) {
    bufferManager->remove_buffer(vertexBufferName);
    bufferManager->remove_buffer(indexBufferName);
  }

  std::print("Object - {} - destructor executed\n", identifier);
}

void RenderObject::update_model_matrix() {
  modelMatrix = glm::mat4(1.0f);
  modelMatrix = glm::translate(modelMatrix, position);
  modelMatrix =
      glm::rotate(modelMatrix, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
  modelMatrix =
      glm::rotate(modelMatrix, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
  modelMatrix =
      glm::rotate(modelMatrix, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
  modelMatrix = glm::scale(modelMatrix, scale);
  transformDirty = false;
}

void RenderObject::update_vertices() {
  if (!verticesDirty) {
    return;
  }

  // Transform vertices based on position, rotation, and scale
  for (size_t i = 0; i < originalVertices.size(); ++i) {
    glm::vec2 originalPos = originalVertices[i].pos;

    // Apply scale
    glm::vec2 scaledPos = originalPos * glm::vec2(scale.x, scale.y);

    if (type == ObjectType::OBJECT_2D) {
      // 2D rotation (around Z axis only)
      float cosZ = std::cos(rotation.z);
      float sinZ = std::sin(rotation.z);

      glm::vec2 rotatedPos;
      rotatedPos.x = (scaledPos.x * cosZ) - (scaledPos.y * sinZ);
      rotatedPos.y = (scaledPos.x * sinZ) + (scaledPos.y * cosZ);

      // Apply translation
      transformedVertices[i].pos =
          rotatedPos + glm::vec2(position.x, position.y);
    } else {
      // 3D rotation (around X, Y, Z axes)
      // Convert 2D position to 3D for rotation
      glm::vec3 pos3d = glm::vec3(scaledPos.x, scaledPos.y, 0.0f);

      // Rotate around X axis
      float cosX = std::cos(rotation.x);
      float sinX = std::sin(rotation.x);
      glm::vec3 rotatedX;
      rotatedX.x = pos3d.x;
      rotatedX.y = (pos3d.y * cosX) - (pos3d.z * sinX);
      rotatedX.z = (pos3d.y * sinX) + (pos3d.z * cosX);

      // Rotate around Y axis
      float cosY = std::cos(rotation.y);
      float sinY = std::sin(rotation.y);
      glm::vec3 rotatedY;
      rotatedY.x = (rotatedX.x * cosY) + (rotatedX.z * sinY);
      rotatedY.y = rotatedX.y;
      rotatedY.z = (-rotatedX.x * sinY) + (rotatedX.z * cosY);

      // Rotate around Z axis
      float cosZ = std::cos(rotation.z);
      float sinZ = std::sin(rotation.z);
      glm::vec3 rotatedZ;
      rotatedZ.x = (rotatedY.x * cosZ) - (rotatedY.y * sinZ);
      rotatedZ.y = (rotatedY.x * sinZ) + (rotatedY.y * cosZ);
      rotatedZ.z = rotatedY.z;

      // Project back to 2D and apply translation
      transformedVertices[i].pos =
          glm::vec2(rotatedZ.x, rotatedZ.y) + glm::vec2(position.x, position.y);
    }

    // Keep the same color
    transformedVertices[i].color = originalVertices[i].color;
  }

  // Update the vertex buffer with transformed vertices
  Buffer *vertexBuffer = bufferManager->get_buffer(vertexBufferName);
  if (vertexBuffer) {
    vertexBuffer->update_data(
        transformedVertices.data(),
        transformedVertices.size() * sizeof(Material::Vertex2D), 0);
  }

  verticesDirty = false;
}

void RenderObject::restore_original_vertices() {
  // Restore original vertex positions to the buffer
  Buffer *vertexBuffer = bufferManager->get_buffer(vertexBufferName);
  if (vertexBuffer) {
    vertexBuffer->update_data(
        originalVertices.data(),
        originalVertices.size() * sizeof(Material::Vertex2D), 0);
  }

  // Reset transformed vertices to original
  transformedVertices = originalVertices;
  verticesDirty = false;
}

void RenderObject::draw(vk::raii::CommandBuffer &commandBuffer,
                        uint32_t deviceIndex, uint32_t frameIndex) {
  if (!visible) {
    return;
  }

  if (!material) {
    std::print("Warning: Cannot draw object '{}' - no material assigned\n",
               identifier);
    return;
  }

  if (!material->is_initialized()) {
    std::print("Warning: Cannot draw object '{}' - material '{}' not "
               "initialized\n",
               identifier, materialIdentifier);
    return;
  }

  // Update transformations based on mode
  if (transformMode == TransformMode::CPU_VERTICES) {
    // CPU-side: Update vertices if needed
    // Only update if we have vertex data (non-textured objects)
    if (verticesDirty && !originalVertices.empty()) {
      update_vertices();
    }
  } else {
    // GPU-side: Update model matrix if needed
    if (transformDirty) {
      update_model_matrix();
    }
    
    // Update UBO with object's transformation
    // Determine which UBO buffer to use based on material
    std::string uboBufferName;
    if (materialIdentifier == "Textured") {
      uboBufferName = "textured_ubo";
    } else if (materialIdentifier == "Test") {
      uboBufferName = "material-test";
    }
    
    if (!uboBufferName.empty()) {
      Buffer *uboBuffer = bufferManager->get_buffer(uboBufferName);
      if (uboBuffer) {
        // Prepare transformation data
        struct TransformUBO {
          glm::mat4 model;
          glm::mat4 view;
          glm::mat4 proj;
        };
        
        TransformUBO uboData = {
          .model = modelMatrix,
          .view = glm::mat4(1.0f),   // Identity for 2D
          .proj = glm::mat4(1.0f)    // Identity for 2D (using NDC)
        };
        
        // Update the UBO buffer with this object's transformation
        uboBuffer->update_data(&uboData, sizeof(TransformUBO), 0);
      }
    }
  }

  // Bind material
  // Note: Textures are bound during object creation, not during draw
  // to avoid updating descriptor sets while command buffer is recording
  material->bind(commandBuffer, deviceIndex, frameIndex);

  // Bind vertex buffer
  Buffer *vertexBuffer = bufferManager->get_buffer(vertexBufferName);
  if (vertexBuffer) {
    vertexBuffer->bind_vertex(commandBuffer, 0, 0, deviceIndex);
  }

  // Bind index buffer
  Buffer *indexBuffer = bufferManager->get_buffer(indexBufferName);
  if (indexBuffer) {
    indexBuffer->bind_index(commandBuffer, vk::IndexType::eUint16, 0,
                            deviceIndex);
  }

  // Draw
  commandBuffer.drawIndexed(indexCount, 1, 0, 0, 0);
}

void RenderObject::set_position(const glm::vec3 &pos) {
  position = pos;
  transformDirty = true;
  verticesDirty = true;
}

void RenderObject::set_rotation(const glm::vec3 &rot) {
  rotation = rot;
  transformDirty = true;
  verticesDirty = true;
}

void RenderObject::set_scale(const glm::vec3 &scl) {
  scale = scl;
  transformDirty = true;
  verticesDirty = true;
}

void RenderObject::set_visible(bool vis) { visible = vis; }

void RenderObject::set_transform_mode(RenderObject::TransformMode mode) {
  if (transformMode == mode) {
    return;
  }

  transformMode = mode;

  if (mode == TransformMode::CPU_VERTICES) {
    // Switching to CPU mode: mark vertices as dirty to apply transformations
    // Only apply if we have vertex data (non-textured objects)
    if (!originalVertices.empty()) {
      verticesDirty = true;
    }
  } else {
    // Switching to GPU mode: restore original vertices and mark matrix as dirty
    // Only restore if we have vertex data (non-textured objects)
    if (!originalVertices.empty()) {
      restore_original_vertices();
    }
    transformDirty = true;
  }

  std::print("Object '{}' transform mode changed to: {}\n", identifier,
             mode == TransformMode::CPU_VERTICES ? "CPU_VERTICES"
                                                 : "GPU_MATRIX");
}

const std::string &RenderObject::get_identifier() const { return identifier; }

RenderObject::ObjectType RenderObject::get_type() const { return type; }

bool RenderObject::is_visible() const { return visible; }

const glm::mat4 &RenderObject::get_model_matrix() {
  if (transformDirty) {
    update_model_matrix();
  }
  return modelMatrix;
}

Material *RenderObject::get_material() const { return material; }

RenderObject::TransformMode RenderObject::get_transform_mode() const {
  return transformMode;
}
