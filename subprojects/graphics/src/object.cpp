#include "object.h"
#include "buffer_manager.h"
#include "config.h"
#include "material.h"
#include "material_manager.h"
#include "texture_manager.h"
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <print>
#include <vector>
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

render::Object::Object(const ObjectCreateInfo &createInfo,
                       device::BufferManager *bufferManager,
                       MaterialManager *materialManager,
                       TextureManager *textureManager)
    : identifier(createInfo.identifier), type(createInfo.type),
      indexCount(createInfo.indices.size()),
      materialIdentifier(createInfo.materialIdentifier),
      textureIdentifier(createInfo.textureIdentifier),
      useSubmeshes(!createInfo.submeshes.empty()),
      position(createInfo.position), rotation(createInfo.rotation),
      scale(createInfo.scale), transformDirty(true),
      visible(createInfo.visible), bufferManager(bufferManager),
      materialManager(materialManager), textureManager(textureManager),
      rotationMode(type == ObjectType::OBJECT_2D ? RotationMode::SHADER_2D
                                                 : RotationMode::TRANSFORM_3D) {
  // Get logical devices for descriptor set creation
  auto *deviceManager = materialManager->get_device_manager();
  if (deviceManager) {
    const auto &devices = deviceManager->get_all_logical_devices();
    for (auto *device : devices) {
      logicalDevices.push_back(const_cast<device::LogicalDevice *>(device));
    }
  }

  // Create unique buffer names for this object
  vertexBufferName = identifier + "_vertices";
  indexBufferName = identifier + "_indices";

  // Create vertex buffer based on vertex type
  std::visit(
      [&](auto &&vertices) {
        using T = std::decay_t<decltype(vertices)>;
        using VertexType = typename T::value_type;

        device::Buffer::BufferCreateInfo vertInfo = {
            .identifier = vertexBufferName,
            .type = device::Buffer::BufferType::VERTEX,
            .usage = device::Buffer::BufferUsage::DYNAMIC,
            .size = vertices.size() * sizeof(VertexType),
            .elementSize = sizeof(VertexType),
            .initialData = vertices.data()};

        bufferManager->create_buffer(vertInfo);
      },
      createInfo.vertices);

  // Create index buffer
  device::Buffer::BufferCreateInfo indInfo = {
      .identifier = indexBufferName,
      .type = device::Buffer::BufferType::INDEX,
      .usage = device::Buffer::BufferUsage::STATIC,
      .size = createInfo.indices.size() * sizeof(uint16_t),
      .initialData = createInfo.indices.data()};

  bufferManager->create_buffer(indInfo);

  // Setup materials
  if (useSubmeshes) {
    // Multi-material mode: setup submeshes with their materials
    submeshes = createInfo.submeshes;
    setup_materials_for_submeshes(submeshes);

    // Also setup the base material for submeshes that don't specify one
    material = materialManager->get_material(materialIdentifier);

    if (!material) {
      std::print("Warning: Base material '{}' not found for object '{}'\n",
                 materialIdentifier, identifier);
    }
  } else {
    // Single material mode
    material = materialManager->get_material(materialIdentifier);
  }

  if (!material) {
    std::print("Warning: Material '{}' not found for object '{}'\n",
               materialIdentifier, identifier);
  }

  // Create per-object UBO for Test and Test2D materials to avoid sharing
  // transforms
  if ((materialIdentifier == "Test" || materialIdentifier == "Test2D") &&
      !useSubmeshes) {
    device::Buffer::TransformUBO uboData = {.model = glm::mat4(1.0f),
                                            .view = glm::mat4(1.0f),
                                            .proj = glm::mat4(1.0f)};

    std::string uboBufferName = materialIdentifier + "_" + identifier + "_ubo";
    device::Buffer::BufferCreateInfo uboInfo = {
        .identifier = uboBufferName,
        .type = device::Buffer::BufferType::UNIFORM,
        .usage = device::Buffer::BufferUsage::DYNAMIC,
        .size = sizeof(device::Buffer::TransformUBO),
        .elementSize = sizeof(device::Buffer::TransformUBO),
        .initialData = &uboData};

    bufferManager->create_buffer(uboInfo);
  }

  // Create per-object descriptor sets
  create_descriptor_sets();

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

void render::Object::setup_materials_for_submeshes(
    std::vector<Submesh> &submeshes) {

  for (auto submesh : submeshes) {
    submesh.material =
        materialManager->get_material(submesh.materialIdentifier);
    if (!submesh.material) {
      std::print(
          "Warning: Material '{}' not found for submesh in object '{}'\n",
          submesh.materialIdentifier, identifier);
    }
  }
}

std::string
render::Object::get_ubo_buffer_name(const std::string &matIdentifier) const {
  if (matIdentifier == "Textured" || matIdentifier.find("Textured_") == 0) {
    return matIdentifier + "_ubo";
  } else if (matIdentifier == "Test" || matIdentifier == "Test2D") {
    // Per-object UBO for Test and Test2D materials - must match naming in
    // constructor (line 151)
    return matIdentifier + "_" + identifier + "_ubo";
  }
  return "";
}

void render::Object::create_descriptor_sets() {
  if (!material) {
    std::print("Warning: Cannot create descriptor sets for object '{}' - no "
               "material\n",
               identifier);
    return;
  }

  std::print("Creating descriptor sets for object '{}' with material '{}'\n",
             identifier, materialIdentifier);

  // Create descriptor sets for each device
  descriptorSets.reserve(logicalDevices.size());

  for (size_t deviceIdx = 0; deviceIdx < logicalDevices.size(); ++deviceIdx) {
    auto *device = logicalDevices[deviceIdx];

    // Get descriptor set layout from material
    const vk::DescriptorSetLayout &layout =
        *material->get_descriptor_set_layout(deviceIdx);

    // Allocate descriptor sets (one per frame)
    uint32_t maxFrames = general::Config::get_instance().get_max_frames();
    std::vector<vk::DescriptorSetLayout> layouts(maxFrames, layout);

    vk::DescriptorSetAllocateInfo allocInfo{.descriptorPool =
                                                *device->get_descriptor_pool(),
                                            .descriptorSetCount = maxFrames,
                                            .pSetLayouts = layouts.data()};

    descriptorSets.emplace_back(device->get_device(), allocInfo);
    std::print("  Allocated {} descriptor sets for device {}\n", maxFrames,
               deviceIdx);
  }

  std::print("  Total devices: {}, descriptorSets size: {}\n",
             logicalDevices.size(), descriptorSets.size());

  // Bind uniform buffer first (binding 0)
  for (size_t deviceIdx = 0; deviceIdx < logicalDevices.size(); ++deviceIdx) {
    std::string uboName = get_ubo_buffer_name(materialIdentifier);
    if (uboName.empty()) {
      std::print("Warning: No UBO name for object '{}' with material '{}'\n",
                 identifier, materialIdentifier);
      continue;
    }

    auto *uboBuffer = bufferManager->get_buffer(uboName);
    if (!uboBuffer) {
      std::print("Warning: UBO buffer '{}' not found for object '{}'\n",
                 uboName, identifier);
      continue;
    }

    bind_buffer_to_descriptor_sets(uboBuffer, 0, deviceIdx);
  }

  // Bind texture if this is a textured object (binding 1)
  if (textureManager && !textureIdentifier.empty()) {
    auto *texture = textureManager->get_texture(textureIdentifier);
    if (!texture) {
      std::print("Warning: Texture '{}' not found for object '{}'\n",
                 textureIdentifier, identifier);
    } else if (!texture->get_image()) {
      std::print("Warning: Texture '{}' has no image for object '{}'\n",
                 textureIdentifier, identifier);
    } else {
      for (size_t deviceIdx = 0; deviceIdx < logicalDevices.size();
           ++deviceIdx) {
        bind_texture_to_descriptor_sets(texture->get_image().get(), 1,
                                        deviceIdx);
      }
    }
  } else if (!textureIdentifier.empty()) {
    std::print("Warning: Texture identifier '{}' specified but no texture "
               "manager for object '{}'\n",
               textureIdentifier, identifier);
  }

  std::print("Finished creating descriptor sets for object '{}'\n", identifier);
}

void render::Object::bind_texture_to_descriptor_sets(Image *image,
                                                     uint32_t binding,
                                                     uint32_t deviceIndex) {
  if (!image || deviceIndex >= descriptorSets.size()) {
    std::print("Warning: bind_texture failed for object '{}' - image={}, "
               "deviceIndex={}, descriptorSets.size()={}\n",
               identifier, (void *)image, deviceIndex, descriptorSets.size());
    return;
  }

  std::print("Binding texture for object '{}' to binding {} on device {}, {} "
             "frames\n",
             identifier, binding, deviceIndex,
             descriptorSets[deviceIndex].size());

  // Update all frames for this device
  for (size_t frameIdx = 0; frameIdx < descriptorSets[deviceIndex].size();
       ++frameIdx) {
    vk::DescriptorImageInfo imageInfo{
        .sampler = *image->get_sampler(deviceIndex),
        .imageView = *image->get_image_view(deviceIndex),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};

    vk::WriteDescriptorSet descriptorWrite{
        .dstSet = *descriptorSets[deviceIndex][frameIdx],
        .dstBinding = binding,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .pImageInfo = &imageInfo};

    logicalDevices[deviceIndex]->get_device().updateDescriptorSets(
        descriptorWrite, nullptr);
  }

  std::print("Successfully updated {} descriptor sets for texture binding\n",
             descriptorSets[deviceIndex].size());
}

void render::Object::bind_buffer_to_descriptor_sets(device::Buffer *buffer,
                                                    uint32_t binding,
                                                    uint32_t deviceIndex) {
  if (!buffer || deviceIndex >= descriptorSets.size()) {
    std::print("Warning: bind_buffer failed for object '{}' - buffer={}, "
               "deviceIndex={}, descriptorSets.size()={}\n",
               identifier, (void *)buffer, deviceIndex, descriptorSets.size());
    return;
  }

  VkBuffer vkBuffer = buffer->get_buffer(deviceIndex);
  if (!vkBuffer) {
    std::print("ERROR: Buffer '{}' returned null VkBuffer handle for object "
               "'{}' device {}\n",
               buffer->get_identifier(), identifier, deviceIndex);
    return;
  }

  std::print("Binding UBO for object '{}' (buffer='{}', handle={}) to binding "
             "{} on device {}, {} frames\n",
             identifier, buffer->get_identifier(), (void *)vkBuffer, binding,
             deviceIndex, descriptorSets[deviceIndex].size());

  // Update all frames for this device
  for (size_t frameIdx = 0; frameIdx < descriptorSets[deviceIndex].size();
       ++frameIdx) {
    vk::DescriptorBufferInfo bufferInfo{
        .buffer = vkBuffer, .offset = 0, .range = buffer->get_size()};

    vk::WriteDescriptorSet descriptorWrite{
        .dstSet = *descriptorSets[deviceIndex][frameIdx],
        .dstBinding = binding,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .pBufferInfo = &bufferInfo};

    logicalDevices[deviceIndex]->get_device().updateDescriptorSets(
        descriptorWrite, nullptr);
  }

  std::print("Successfully updated {} descriptor sets for UBO binding\n",
             descriptorSets[deviceIndex].size());
}

void render::Object::draw(vk::raii::CommandBuffer &commandBuffer,
                          uint32_t deviceIndex, uint32_t frameIndex) {
  if (!visible) {
    return;
  }

  // Update model matrix if needed (GPU will use this via shaders)
  if (transformDirty) {
    update_model_matrix();
  }

  // Bind vertex buffer (shared across all submeshes)
  device::Buffer *vertexBuffer = bufferManager->get_buffer(vertexBufferName);
  if (vertexBuffer) {
    vertexBuffer->bind_vertex(commandBuffer, 0, 0, deviceIndex);
  }

  // Bind index buffer (shared across all submeshes)
  device::Buffer *indexBuffer = bufferManager->get_buffer(indexBufferName);
  if (indexBuffer) {
    indexBuffer->bind_index(commandBuffer, vk::IndexType::eUint16, 0,
                            deviceIndex);
  }

  if (useSubmeshes) {
    // Multi-material mode: draw each submesh with its own material or base
    // material
    for (const auto &submesh : submeshes) {
      // Use submesh material if available, otherwise use base material
      Material *useMaterial = submesh.material ? submesh.material : material;

      if (!useMaterial) {
        std::print("Warning: Cannot draw submesh in object '{}' - no material "
                   "assigned and no base material\n",
                   identifier);
        continue;
      }

      if (!useMaterial->is_initialized()) {
        std::print("Warning: Cannot draw submesh in object '{}' - material "
                   "'{}' not initialized\n",
                   identifier, useMaterial->get_identifier());
        continue;
      }

      // Update UBO with object's transformation for this material
      std::string uboBufferName =
          get_ubo_buffer_name(useMaterial->get_identifier());

      if (!uboBufferName.empty()) {
        device::Buffer *uboBuffer = bufferManager->get_buffer(uboBufferName);
        if (uboBuffer) {
          device::Buffer::TransformUBO uboData = {.model = modelMatrix,
                                                  .view = glm::mat4(1.0f),
                                                  .proj = glm::mat4(1.0f)};

          uboBuffer->update_data(&uboData, sizeof(device::Buffer::TransformUBO),
                                 0);
        }
      }

      // Bind material for this submesh with object's descriptor set
      vk::raii::DescriptorSet *descriptorSet = nullptr;
      if (deviceIndex < descriptorSets.size() &&
          frameIndex < descriptorSets[deviceIndex].size()) {
        descriptorSet = &descriptorSets[deviceIndex][frameIndex];
      }
      useMaterial->bind(commandBuffer, deviceIndex, descriptorSet);

      // Draw this submesh with its specific index range
      commandBuffer.drawIndexed(submesh.indexCount, 1, submesh.indexStart, 0,
                                0);
    }
  } else {
    // Single material mode (backward compatibility)
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

    // Update UBO with object's transformation
    std::string uboBufferName = get_ubo_buffer_name(materialIdentifier);

    if (!uboBufferName.empty()) {
      device::Buffer *uboBuffer = bufferManager->get_buffer(uboBufferName);
      if (uboBuffer) {
        device::Buffer::TransformUBO uboData = {.model = modelMatrix,
                                                .view = glm::mat4(1.0f),
                                                .proj = glm::mat4(1.0f)};

        uboBuffer->update_data(&uboData, sizeof(device::Buffer::TransformUBO),
                               0);
      }
    }

    // Bind material with object's descriptor set
    vk::raii::DescriptorSet *descriptorSet = nullptr;
    if (deviceIndex < descriptorSets.size() &&
        frameIndex < descriptorSets[deviceIndex].size()) {
      descriptorSet = &descriptorSets[deviceIndex][frameIndex];
    }
    material->bind(commandBuffer, deviceIndex, descriptorSet);

    // Draw
    commandBuffer.drawIndexed(indexCount, 1, 0, 0, 0);
  }
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
