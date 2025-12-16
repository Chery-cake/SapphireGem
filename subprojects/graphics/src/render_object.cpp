#include "render_object.h"
#include "buffer_manager.h"
#include "material.h"
#include "material_manager.h"
#include "texture_manager.h"
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <print>
#include <vulkan/vulkan_raii.hpp>

namespace SapphireGem::Graphics {

Object::Object(const ObjectCreateInfo createInfo,
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
      rotationMode(type == ObjectType::OBJECT_2D ? RotationMode::TRANSFORM_2D : RotationMode::TRANSFORM_3D) {
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

  // Create per-object UBO for Test material to avoid sharing transforms
  if (materialIdentifier == "Test") {
    struct TransformUBO {
      glm::mat4 model;
      glm::mat4 view;
      glm::mat4 proj;
    };

    TransformUBO uboData = {
        .model = glm::mat4(1.0f),
        .view = glm::mat4(1.0f),
        .proj = glm::mat4(1.0f)
    };

    std::string uboBufferName = materialIdentifier + "_" + identifier + "_ubo";
    Buffer::BufferCreateInfo uboInfo = {
        .identifier = uboBufferName,
        .type = Buffer::BufferType::UNIFORM,
        .usage = Buffer::BufferUsage::DYNAMIC,
        .size = sizeof(TransformUBO),
        .elementSize = sizeof(TransformUBO),
        .initialData = &uboData};

    bufferManager->create_buffer(uboInfo);
    
    // Bind the UBO to the material
    Buffer *uboBuffer = bufferManager->get_buffer(uboBufferName);
    if (material && uboBuffer) {
      material->bind_uniform_buffer(uboBuffer, 0, 0);
    }
  }

  update_model_matrix();
}

Object::Object(const ObjectCreateInfoTextured createInfo,
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
      rotationMode(type == ObjectType::OBJECT_2D ? RotationMode::SHADER_2D : RotationMode::TRANSFORM_3D) {
  // Create unique buffer names for this object
  vertexBufferName = identifier + "_vertices";
  indexBufferName = identifier + "_indices";

  // Create vertex buffer for textured vertices
  // Use DYNAMIC for textured objects since they don't use vertex
  // transformation but the buffer might need updates in the future
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
}

Object::~Object() {
  if (bufferManager) {
    bufferManager->remove_buffer(vertexBufferName);
    bufferManager->remove_buffer(indexBufferName);
  }

  std::print("Object - {} - destructor executed\n", identifier);
}

void Object::update_model_matrix() {
  modelMatrix = glm::mat4(1.0f);
  modelMatrix = glm::translate(modelMatrix, position);
  
  // Apply rotation based on mode
  if (rotationMode == RotationMode::SHADER_2D) {
    // For shader-based 2D rotation, only apply Z rotation
    modelMatrix = glm::rotate(modelMatrix, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
  } else if (rotationMode == RotationMode::TRANSFORM_2D) {
    // For 2D transformation, can apply X, Y rotations for projection effects
    modelMatrix = glm::rotate(modelMatrix, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
    modelMatrix = glm::rotate(modelMatrix, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
    modelMatrix = glm::rotate(modelMatrix, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
  } else {
    // For 3D transformation, rotate around all axes
    modelMatrix = glm::rotate(modelMatrix, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
    modelMatrix = glm::rotate(modelMatrix, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
    modelMatrix = glm::rotate(modelMatrix, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
  }
  
  modelMatrix = glm::scale(modelMatrix, scale);
  transformDirty = false;
}

void Object::draw(vk::raii::CommandBuffer &commandBuffer,
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
  // Determine which UBO buffer to use based on material and object identifier
  std::string uboBufferName;
  if (materialIdentifier == "Textured" || materialIdentifier.find("Textured_") == 0) {
    uboBufferName = materialIdentifier + "_ubo";
  } else if (materialIdentifier == "Test") {
    // Create per-object UBO for Test material to avoid sharing transforms
    uboBufferName = materialIdentifier + "_" + identifier + "_ubo";
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

void Object::set_position(const glm::vec3 &pos) {
  position = pos;
  transformDirty = true;
}

void Object::set_rotation(const glm::vec3 &rot) {
  rotation = rot;
  transformDirty = true;
}

void Object::set_scale(const glm::vec3 &scl) {
  scale = scl;
  transformDirty = true;
}

void Object::set_visible(bool vis) { visible = vis; }

void Object::set_rotation_mode(Object::RotationMode mode) {
  if (rotationMode == mode) {
    return;
  }

  rotationMode = mode;
  transformDirty = true;

  std::print("Object '{}' rotation mode changed to: {}\n", identifier,
             mode == RotationMode::SHADER_2D ? "SHADER_2D" :
             mode == RotationMode::TRANSFORM_2D ? "TRANSFORM_2D" : "TRANSFORM_3D");
}

void Object::rotate_2d(float angle) {
  // For shader-based 2D rotation (Z-axis only)
  rotation.z = angle;
  transformDirty = true;
}

void Object::rotate(const glm::vec3 &angles) {
  // For 2D/3D rotation based on current mode
  if (rotationMode == RotationMode::SHADER_2D) {
    // Shader-based 2D rotation: only Z-axis
    rotation.z = angles.z;
  } else if (rotationMode == RotationMode::TRANSFORM_2D) {
    // Transform 2D: can use X, Y for projection effects, Z for in-plane rotation
    rotation = angles;
  } else {
    // 3D rotation: all axes
    rotation = angles;
  }
  transformDirty = true;
}

const std::string &Object::get_identifier() const { return identifier; }

Object::ObjectType Object::get_type() const { return type; }

bool Object::is_visible() const { return visible; }

const glm::mat4 &Object::get_model_matrix() {
  if (transformDirty) {
    update_model_matrix();
  }
  return modelMatrix;
}

Material *Object::get_material() const { return material; }

Object::RotationMode Object::get_rotation_mode() const {
  return rotationMode;
}

} // namespace SapphireGem::Graphics
