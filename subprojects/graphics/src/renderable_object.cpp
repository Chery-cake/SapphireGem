#include "renderable_object.h"
#include "buffer_manager.h"
#include "material_manager.h"
#include <glm/gtc/matrix_transform.hpp>
#include <print>

RenderableObject::RenderableObject(const CreateInfo &createInfo,
                                   BufferManager *bufferMgr,
                                   MaterialManager *materialMgr)
    : identifier(createInfo.identifier), type(createInfo.type),
      indexCount(createInfo.indices.size()),
      materialIdentifier(createInfo.materialIdentifier),
      position(createInfo.position), rotation(createInfo.rotation),
      scale(createInfo.scale), transformDirty(true),
      visible(createInfo.visible), bufferManager(bufferMgr),
      materialManager(materialMgr) {

  // Create unique buffer names for this object
  vertexBufferName = identifier + "_vertices";
  indexBufferName = identifier + "_indices";

  // Create vertex buffer
  Buffer::BufferCreateInfo vertInfo = {
      .identifier = vertexBufferName,
      .type = Buffer::BufferType::VERTEX,
      .usage = Buffer::BufferUsage::STATIC,
      .size = createInfo.vertices.size(),
      .initialData = createInfo.vertices.data()};

  bufferManager->create_buffer(vertInfo);

  // Create index buffer
  Buffer::BufferCreateInfo indInfo = {
      .identifier = indexBufferName,
      .type = Buffer::BufferType::INDEX,
      .usage = Buffer::BufferUsage::STATIC,
      .size = createInfo.indices.size(),
      .initialData = createInfo.indices.data()};

  bufferManager->create_buffer(indInfo);

  // Get material reference (shared)
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

RenderableObject::~RenderableObject() {
  // Clean up buffers
  if (bufferManager) {
    bufferManager->remove_buffer(vertexBufferName);
    bufferManager->remove_buffer(indexBufferName);
  }
}

void RenderableObject::update_model_matrix() {
  modelMatrix = glm::mat4(1.0f);
  modelMatrix = glm::translate(modelMatrix, position);
  modelMatrix = glm::rotate(modelMatrix, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
  modelMatrix = glm::rotate(modelMatrix, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
  modelMatrix = glm::rotate(modelMatrix, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
  modelMatrix = glm::scale(modelMatrix, scale);
  transformDirty = false;
}

void RenderableObject::draw(vk::raii::CommandBuffer &commandBuffer,
                            uint32_t deviceIndex, uint32_t frameIndex) {
  if (!visible || !material) {
    return;
  }

  // Bind material
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

void RenderableObject::set_position(const glm::vec3 &pos) {
  position = pos;
  transformDirty = true;
}

void RenderableObject::set_rotation(const glm::vec3 &rot) {
  rotation = rot;
  transformDirty = true;
}

void RenderableObject::set_scale(const glm::vec3 &scl) {
  scale = scl;
  transformDirty = true;
}

void RenderableObject::set_visible(bool vis) { visible = vis; }

const glm::mat4 &RenderableObject::get_model_matrix() {
  if (transformDirty) {
    update_model_matrix();
  }
  return modelMatrix;
}
