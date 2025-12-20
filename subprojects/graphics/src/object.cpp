#include "object.h"
#include "buffer_manager.h"
#include "material.h"
#include "material_manager.h"
#include "texture_manager.h"
#include <array>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <print>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

// Vertex2D implementation
vk::VertexInputBindingDescription
render::Object::Vertex2D::getBindingDescription() {
  return {0, sizeof(Object::Vertex2D), vk::VertexInputRate::eVertex};
}

std::array<vk::VertexInputAttributeDescription, 2>
render::Object::Vertex2D::getAttributeDescriptions() {
  return {
      vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32Sfloat,
                                          offsetof(Object::Vertex2D, pos)),
      vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat,
                                          offsetof(Object::Vertex2D, color))};
}

// Vertex2DTextured implementation
vk::VertexInputBindingDescription
render::Object::Vertex2DTextured::getBindingDescription() {
  return {0, sizeof(Object::Vertex2DTextured), vk::VertexInputRate::eVertex};
}

std::array<vk::VertexInputAttributeDescription, 3>
render::Object::Vertex2DTextured::getAttributeDescriptions() {
  return {vk::VertexInputAttributeDescription(
              0, 0, vk::Format::eR32G32Sfloat,
              offsetof(Object::Vertex2DTextured, pos)),
          vk::VertexInputAttributeDescription(
              1, 0, vk::Format::eR32G32Sfloat,
              offsetof(Object::Vertex2DTextured, texCoord)),
          vk::VertexInputAttributeDescription(
              2, 0, vk::Format::eR32G32B32Sfloat,
              offsetof(Object::Vertex2DTextured, color))};
}

// Vertex3D implementation
vk::VertexInputBindingDescription
render::Object::Vertex3D::getBindingDescription() {
  return {0, sizeof(Object::Vertex3D), vk::VertexInputRate::eVertex};
}

std::array<vk::VertexInputAttributeDescription, 2>
render::Object::Vertex3D::getAttributeDescriptions() {
  return {
      vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat,
                                          offsetof(Object::Vertex3D, pos)),
      vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat,
                                          offsetof(Object::Vertex3D, color))};
}

// Vertex3DTextured implementation
vk::VertexInputBindingDescription
render::Object::Vertex3DTextured::getBindingDescription() {
  return {0, sizeof(Object::Vertex3DTextured), vk::VertexInputRate::eVertex};
}

std::array<vk::VertexInputAttributeDescription, 3>
render::Object::Vertex3DTextured::getAttributeDescriptions() {
  return {vk::VertexInputAttributeDescription(
              0, 0, vk::Format::eR32G32B32Sfloat,
              offsetof(Object::Vertex3DTextured, pos)),
          vk::VertexInputAttributeDescription(
              1, 0, vk::Format::eR32G32Sfloat,
              offsetof(Object::Vertex3DTextured, texCoord)),
          vk::VertexInputAttributeDescription(
              2, 0, vk::Format::eR32G32B32Sfloat,
              offsetof(Object::Vertex3DTextured, color))};
}

render::Object::Object(const ObjectCreateInfo createInfo,
                       device::BufferManager *bufferManager,
                       MaterialManager *materialManager,
                       TextureManager *textureManager)
    : identifier(createInfo.identifier), type(createInfo.type),
      indexCount(createInfo.indices.size()),
      materialIdentifier(createInfo.materialIdentifier),
      position(createInfo.position), rotation(createInfo.rotation),
      scale(createInfo.scale), transformDirty(true),
      visible(createInfo.visible), bufferManager(bufferManager),
      materialManager(materialManager), textureManager(textureManager),
      rotationMode(type == ObjectType::OBJECT_2D ? RotationMode::SHADER_2D
                                                 : RotationMode::TRANSFORM_3D) {
  // Create unique buffer names for this object
  vertexBufferName = identifier + "_vertices";
  indexBufferName = identifier + "_indices";

  // Create vertex buffer
  device::Buffer::BufferCreateInfo vertInfo = {
      .identifier = vertexBufferName,
      .type = device::Buffer::BufferType::VERTEX,
      .usage = device::Buffer::BufferUsage::DYNAMIC,
      .size = createInfo.vertices.size() * sizeof(Object::Vertex2D),
      .elementSize = sizeof(Object::Vertex2D),
      .initialData = createInfo.vertices.data()};

  bufferManager->create_buffer(vertInfo);

  // Create index buffer
  device::Buffer::BufferCreateInfo indInfo = {
      .identifier = indexBufferName,
      .type = device::Buffer::BufferType::INDEX,
      .usage = device::Buffer::BufferUsage::STATIC,
      .size = createInfo.indices.size() * sizeof(uint16_t),
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

  // Create per-object UBO for Test material to avoid sharing transforms
  if (materialIdentifier == "Test") {
    struct TransformUBO {
      glm::mat4 model;
      glm::mat4 view;
      glm::mat4 proj;
    };

    TransformUBO uboData = {.model = glm::mat4(1.0f),
                            .view = glm::mat4(1.0f),
                            .proj = glm::mat4(1.0f)};

    std::string uboBufferName = materialIdentifier + "_" + identifier + "_ubo";
    device::Buffer::BufferCreateInfo uboInfo = {
        .identifier = uboBufferName,
        .type = device::Buffer::BufferType::UNIFORM,
        .usage = device::Buffer::BufferUsage::DYNAMIC,
        .size = sizeof(TransformUBO),
        .elementSize = sizeof(TransformUBO),
        .initialData = &uboData};

    bufferManager->create_buffer(uboInfo);

    // Bind the UBO to the material
    device::Buffer *uboBuffer = bufferManager->get_buffer(uboBufferName);
    if (material && uboBuffer) {
      material->bind_uniform_buffer(uboBuffer, 0, 0);
    }
  }

  update_model_matrix();
}

render::Object::Object(const ObjectCreateInfoTextured createInfo,
                       device::BufferManager *bufferManager,
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
      rotationMode(type == ObjectType::OBJECT_2D ? RotationMode::SHADER_2D
                                                 : RotationMode::TRANSFORM_3D) {
  // Create unique buffer names for this object
  vertexBufferName = identifier + "_vertices";
  indexBufferName = identifier + "_indices";

  // Create vertex buffer for textured vertices
  // Use DYNAMIC for textured objects since they don't use vertex
  // transformation but the buffer might need updates in the future
  device::Buffer::BufferCreateInfo vertInfo = {
      .identifier = vertexBufferName,
      .type = device::Buffer::BufferType::VERTEX,
      .usage = device::Buffer::BufferUsage::DYNAMIC,
      .size = createInfo.vertices.size() * sizeof(Object::Vertex2DTextured),
      .elementSize = sizeof(Object::Vertex2DTextured),
      .initialData = createInfo.vertices.data()};

  bufferManager->create_buffer(vertInfo);

  // Create index buffer
  device::Buffer::BufferCreateInfo indInfo = {
      .identifier = indexBufferName,
      .type = device::Buffer::BufferType::INDEX,
      .usage = device::Buffer::BufferUsage::STATIC,
      .size = createInfo.indices.size() * sizeof(uint16_t),
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
}

render::Object::~Object() {
  if (bufferManager) {
    bufferManager->remove_buffer(vertexBufferName);
    bufferManager->remove_buffer(indexBufferName);
  }

  std::print("Object - {} - destructor executed\n", identifier);
}

void render::Object::update_model_matrix() {
  modelMatrix = glm::mat4(1.0f);
  modelMatrix = glm::translate(modelMatrix, position);

  // Apply rotation based on mode
  if (rotationMode == RotationMode::SHADER_2D) {
    // For shader-based 2D rotation, only apply Z rotation
    modelMatrix =
        glm::rotate(modelMatrix, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
  } else if (rotationMode == RotationMode::TRANSFORM_2D) {
    // For 2D transformation, can apply X, Y rotations for projection
    // effects
    modelMatrix =
        glm::rotate(modelMatrix, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
    modelMatrix =
        glm::rotate(modelMatrix, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
    modelMatrix =
        glm::rotate(modelMatrix, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
  } else {
    // For 3D transformation, rotate around all axes
    modelMatrix =
        glm::rotate(modelMatrix, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
    modelMatrix =
        glm::rotate(modelMatrix, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
    modelMatrix =
        glm::rotate(modelMatrix, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
  }

  modelMatrix = glm::scale(modelMatrix, scale);
  transformDirty = false;
}

void render::Object::draw(vk::raii::CommandBuffer &commandBuffer,
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

  // Hybrid rendering: GPU does transformations, CPU does auxiliary work
  // Update model matrix if needed (GPU will use this via shaders)
  if (transformDirty) {
    update_model_matrix();
  }

  // Update UBO with object's transformation
  // Determine which UBO buffer to use based on material
  std::string uboBufferName;
  if (materialIdentifier == "Textured" ||
      materialIdentifier.find("Textured_") == 0) {
    uboBufferName = materialIdentifier + "_ubo";
  } else if (materialIdentifier == "Test") {
    // Use shared material UBO - we'll update its data per object
    uboBufferName = "material-test";
  }

  if (!uboBufferName.empty()) {
    device::Buffer *uboBuffer = bufferManager->get_buffer(uboBufferName);
    if (uboBuffer) {
      // Prepare transformation data
      struct TransformUBO {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
      };

      TransformUBO uboData = {
          .model = modelMatrix,
          .view = glm::mat4(1.0f), // Identity for 2D
          .proj = glm::mat4(1.0f)  // Identity for 2D (using NDC)
      };

      // Update the UBO buffer with this object's transformation
      uboBuffer->update_data(&uboData, sizeof(TransformUBO), 0);
    }
  }

  // Bind material
  // Note: Textures are bound during object creation, not during draw
  // to avoid updating descriptor sets while command buffer is recording
  material->bind(commandBuffer, deviceIndex, frameIndex);

  // Bind vertex buffer
  device::Buffer *vertexBuffer = bufferManager->get_buffer(vertexBufferName);
  if (vertexBuffer) {
    vertexBuffer->bind_vertex(commandBuffer, 0, 0, deviceIndex);
  }

  // Bind index buffer
  device::Buffer *indexBuffer = bufferManager->get_buffer(indexBufferName);
  if (indexBuffer) {
    indexBuffer->bind_index(commandBuffer, vk::IndexType::eUint16, 0,
                            deviceIndex);
  }

  // Draw
  commandBuffer.drawIndexed(indexCount, 1, 0, 0, 0);
}

void render::Object::set_position(const glm::vec3 &pos) {
  position = pos;
  transformDirty = true;
}

void render::Object::set_rotation(const glm::vec3 &rot) {
  rotation = rot;
  transformDirty = true;
}

void render::Object::set_scale(const glm::vec3 &scl) {
  scale = scl;
  transformDirty = true;
}

void render::Object::set_visible(bool vis) { visible = vis; }

void render::Object::set_rotation_mode(Object::RotationMode mode) {
  if (rotationMode == mode) {
    return;
  }

  rotationMode = mode;
  transformDirty = true;

  std::print("Object '{}' rotation mode changed to: {}\n", identifier,
             mode == RotationMode::SHADER_2D      ? "SHADER_2D"
             : mode == RotationMode::TRANSFORM_2D ? "TRANSFORM_2D"
                                                  : "TRANSFORM_3D");
}

void render::Object::rotate_2d(float angle) {
  // For shader-based 2D rotation (Z-axis only)
  rotation.z = angle;
  transformDirty = true;
}

void render::Object::rotate(const glm::vec3 &angles) {
  // For 2D/3D rotation based on current mode
  if (rotationMode == RotationMode::SHADER_2D) {
    // Shader-based 2D rotation: only Z-axis
    rotation.z = angles.z;
  } else if (rotationMode == RotationMode::TRANSFORM_2D) {
    // Transform 2D: can use X, Y for projection effects, Z for in-plane
    // rotation
    rotation = angles;
  } else {
    // 3D rotation: all axes
    rotation = angles;
  }

  transformDirty = true;
}

const std::string &render::Object::get_identifier() const { return identifier; }

render::Object::ObjectType render::Object::get_type() const { return type; }

bool render::Object::is_visible() const { return visible; }

const glm::mat4 &render::Object::get_model_matrix() {
  if (transformDirty) {
    update_model_matrix();
  }
  return modelMatrix;
}

render::Material *render::Object::get_material() const { return material; }

render::Object::RotationMode render::Object::get_rotation_mode() const {
  return rotationMode;
}
