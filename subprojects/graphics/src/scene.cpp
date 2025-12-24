#include "scene.h"
#include "material_manager.h"
#include "object.h"
#include <print>
#include <vector>

render::Scene::Scene(MaterialManager *matMgr, TextureManager *texMgr,
                     device::BufferManager *bufMgr, ObjectManager *objMgr)
    : materialManager(matMgr), textureManager(texMgr), bufferManager(bufMgr),
      objectManager(objMgr) {}

render::Scene::~Scene() { cleanup(); }

render::Object *render::Scene::create_triangle_2d(
    const std::string &identifier, MaterialId materialId,
    const glm::vec3 &position, const glm::vec3 &rotation,
    const glm::vec3 &scale, const std::vector<uint16_t> &customIndices) {
  // Define a 2D triangle vertices (in NDC space, z=0)
  const std::vector<Object::Vertex3D> vertices = {
      {{0.0f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}}, // Top vertex (red)
      {{-0.5f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}}, // Bottom left (green)
      {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}}   // Bottom right (blue)
  };

  // Use custom indices if provided, otherwise use default
  const std::vector<uint16_t> indices =
      customIndices.empty() ? std::vector<uint16_t>{0, 1, 2} : customIndices;

  Object::ObjectCreateInfo createInfo{.identifier = identifier,
                                      .type = Object::ObjectType::OBJECT_2D,
                                      .vertices = vertices,
                                      .indices = indices,
                                      .materialIdentifier =
                                          to_string(materialId),
                                      .position = position,
                                      .rotation = rotation,
                                      .scale = scale,
                                      .visible = true};

  Object *obj = objectManager->create_object(createInfo);
  if (obj) {
    sceneObjects.push_back(obj);
  }
  return obj;
}

render::Object *render::Scene::create_quad_2d(
    const std::string &identifier, MaterialId materialId,
    const std::optional<TextureId> &textureId,
    const std::vector<SubmeshDef> &submeshes, const glm::vec3 &position,
    const glm::vec3 &rotation, const glm::vec3 &scale,
    const std::vector<uint16_t> &customIndices) {
  // Determine if we need textured vertices
  bool useTexture = textureId.has_value() || !submeshes.empty();

  std::vector<uint16_t> indices;

  Object::ObjectCreateInfo createInfo{.identifier = identifier,
                                      .type = Object::ObjectType::OBJECT_2D};

  if (useTexture) {
    // For multi-material or textured quad, use textured vertices
    if (!submeshes.empty()) {
      // Multi-material quad split horizontally into sections
      const std::vector<Object::Vertex2DTextured> vertices = {
          // Left half
          {{-0.5f, -0.5f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
          {{0.0f, -0.5f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
          {{0.0f, 0.5f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}},
          {{-0.5f, 0.5f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}},
          // Right half
          {{0.0f, -0.5f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
          {{0.5f, -0.5f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
          {{0.5f, 0.5f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}},
          {{0.0f, 0.5f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}}};

      indices = customIndices.empty() ? std::vector<uint16_t>{
                                            0, 2, 1, 0, 3, 2, // Left half
                                            4, 6, 5, 4, 7, 6  // Right half
                                        }
                                      : customIndices;

      createInfo.vertices = vertices;

      // Convert SubmeshDef to Object::Submesh
      std::vector<Object::Submesh> objectSubmeshes;
      for (const auto &def : submeshes) {
        Object::Submesh submesh;
        submesh.indexStart = def.indexStart;
        submesh.indexCount = def.indexCount;
        submesh.materialIdentifier = to_string(def.materialId);
        submesh.material = nullptr;
        objectSubmeshes.push_back(submesh);
      }
      createInfo.submeshes = objectSubmeshes;
    } else {
      // Single textured quad
      const std::vector<Object::Vertex2DTextured> vertices = {
          {{-0.5f, -0.5f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}, // Bottom-left
          {{0.5f, -0.5f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},  // Bottom-right
          {{0.5f, 0.5f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}},   // Top-right
          {{-0.5f, 0.5f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}}   // Top-left
      };

      indices = customIndices.empty() ? std::vector<uint16_t>{0, 2, 1, 0, 3, 2}
                                      : customIndices;

      createInfo.vertices = vertices;
    }

    if (textureId.has_value()) {
      createInfo.textureIdentifier = to_string(textureId.value());
    }
  } else {
    // Non-textured quad with colored vertices
    const std::vector<Object::Vertex3D> vertices = {
        {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}}, // Bottom-left (red)
        {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},  // Bottom-right (green)
        {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},   // Top-right (blue)
        {{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 0.0f}}   // Top-left (yellow)
    };

    indices = customIndices.empty() ? std::vector<uint16_t>{0, 2, 1, 0, 3, 2}
                                    : customIndices;

    createInfo.vertices = vertices;
  }

  createInfo.indices = indices;
  createInfo.materialIdentifier = to_string(materialId);
  createInfo.position = position;
  createInfo.rotation = rotation;
  createInfo.scale = scale;
  createInfo.visible = true;

  Object *obj = objectManager->create_object(createInfo);
  if (obj) {
    sceneObjects.push_back(obj);
  }
  return obj;
}

render::Object *render::Scene::create_cube_3d(
    const std::string &identifier, MaterialId materialId,
    const std::optional<TextureId> &textureId,
    const std::vector<SubmeshDef> &submeshes, const glm::vec3 &position,
    const glm::vec3 &rotation, const glm::vec3 &scale,
    const std::vector<uint16_t> &customIndices) {
  constexpr float s = 0.5f; // Half size

  // Determine if we need textured vertices
  bool useTexture = textureId.has_value() || !submeshes.empty();

  std::vector<uint16_t> indices;

  Object::ObjectCreateInfo createInfo{.identifier = identifier,
                                      .type = Object::ObjectType::OBJECT_3D};

  if (useTexture) {
    // Textured vertices for cube
    const std::vector<Object::Vertex3DTextured> vertices = {
        // Front face
        {{-s, -s, s}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}}, // 0
        {{s, -s, s}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},  // 1
        {{s, s, s}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},   // 2
        {{-s, s, s}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},  // 3

        // Back face
        {{-s, -s, -s}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}, // 4
        {{s, -s, -s}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},  // 5
        {{s, s, -s}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},   // 6
        {{-s, s, -s}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},  // 7

        // Left face
        {{-s, -s, -s}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}, // 8
        {{-s, -s, s}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},  // 9
        {{-s, s, s}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},   // 10
        {{-s, s, -s}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},  // 11

        // Right face
        {{s, -s, -s}, {0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}}, // 12
        {{s, -s, s}, {1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}},  // 13
        {{s, s, s}, {1.0f, 1.0f}, {1.0f, 1.0f, 0.0f}},   // 14
        {{s, s, -s}, {0.0f, 1.0f}, {1.0f, 1.0f, 0.0f}},  // 15

        // Top face
        {{-s, s, -s}, {0.0f, 0.0f}, {0.0f, 1.0f, 1.0f}}, // 16
        {{s, s, -s}, {1.0f, 0.0f}, {0.0f, 1.0f, 1.0f}},  // 17
        {{s, s, s}, {1.0f, 1.0f}, {0.0f, 1.0f, 1.0f}},   // 18
        {{-s, s, s}, {0.0f, 1.0f}, {0.0f, 1.0f, 1.0f}},  // 19

        // Bottom face
        {{-s, -s, -s}, {0.0f, 0.0f}, {1.0f, 0.0f, 1.0f}}, // 20
        {{s, -s, -s}, {1.0f, 0.0f}, {1.0f, 0.0f, 1.0f}},  // 21
        {{s, -s, s}, {1.0f, 1.0f}, {1.0f, 0.0f, 1.0f}},   // 22
        {{-s, -s, s}, {0.0f, 1.0f}, {1.0f, 0.0f, 1.0f}}   // 23
    };

    indices =
        customIndices.empty()
            ? std::vector<uint16_t>{// Front face
                                    0, 2, 1, 0, 3, 2,
                                    // Back face
                                    4, 5, 6, 6, 7, 4,
                                    // Left face
                                    8, 10, 9, 8, 11, 10,
                                    // Right face
                                    12, 13, 14, 14, 15, 12,
                                    // Top face
                                    16, 17, 18, 18, 19, 16,
                                    // Bottom face
                                    20, 22, 21, 20, 23, 22}
            : customIndices;

    createInfo.vertices = vertices;

    if (!submeshes.empty()) {
      // Convert SubmeshDef to Object::Submesh
      std::vector<Object::Submesh> objectSubmeshes;
      for (const auto &def : submeshes) {
        Object::Submesh submesh;
        submesh.indexStart = def.indexStart;
        submesh.indexCount = def.indexCount;
        submesh.materialIdentifier = to_string(def.materialId);
        submesh.material = nullptr;
        objectSubmeshes.push_back(submesh);
      }
      createInfo.submeshes = objectSubmeshes;
    }
    if (textureId.has_value()) {
      createInfo.textureIdentifier = to_string(textureId.value());
    }
  } else {
    // Non-textured cube with colored vertices
    const std::vector<Object::Vertex3D> vertices = {
        // Front face (red)
        {{-s, -s, s}, {1.0f, 0.0f, 0.0f}}, // 0
        {{s, -s, s}, {1.0f, 0.0f, 0.0f}},  // 1
        {{s, s, s}, {1.0f, 0.0f, 0.0f}},   // 2
        {{-s, s, s}, {1.0f, 0.0f, 0.0f}},  // 3

        // Back face (green)
        {{-s, -s, -s}, {0.0f, 1.0f, 0.0f}}, // 4
        {{s, -s, -s}, {0.0f, 1.0f, 0.0f}},  // 5
        {{s, s, -s}, {0.0f, 1.0f, 0.0f}},   // 6
        {{-s, s, -s}, {0.0f, 1.0f, 0.0f}},  // 7

        // Left face (blue)
        {{-s, -s, -s}, {0.0f, 0.0f, 1.0f}}, // 8
        {{-s, -s, s}, {0.0f, 0.0f, 1.0f}},  // 9
        {{-s, s, s}, {0.0f, 0.0f, 1.0f}},   // 10
        {{-s, s, -s}, {0.0f, 0.0f, 1.0f}},  // 11

        // Right face (yellow)
        {{s, -s, -s}, {1.0f, 1.0f, 0.0f}}, // 12
        {{s, -s, s}, {1.0f, 1.0f, 0.0f}},  // 13
        {{s, s, s}, {1.0f, 1.0f, 0.0f}},   // 14
        {{s, s, -s}, {1.0f, 1.0f, 0.0f}},  // 15

        // Top face (cyan)
        {{-s, s, -s}, {0.0f, 1.0f, 1.0f}}, // 16
        {{s, s, -s}, {0.0f, 1.0f, 1.0f}},  // 17
        {{s, s, s}, {0.0f, 1.0f, 1.0f}},   // 18
        {{-s, s, s}, {0.0f, 1.0f, 1.0f}},  // 19

        // Bottom face (magenta)
        {{-s, -s, -s}, {1.0f, 0.0f, 1.0f}}, // 20
        {{s, -s, -s}, {1.0f, 0.0f, 1.0f}},  // 21
        {{s, -s, s}, {1.0f, 0.0f, 1.0f}},   // 22
        {{-s, -s, s}, {1.0f, 0.0f, 1.0f}}   // 23
    };

    indices =
        customIndices.empty()
            ? std::vector<uint16_t>{// Front face
                                    0, 2, 1, 0, 3, 2,
                                    // Back face
                                    4, 5, 6, 6, 7, 4,
                                    // Left face
                                    8, 10, 9, 8, 11, 10,
                                    // Right face
                                    12, 13, 14, 14, 15, 12,
                                    // Top face
                                    16, 17, 18, 18, 19, 16,
                                    // Bottom face
                                    20, 22, 21, 20, 23, 22}
            : customIndices;

    createInfo.vertices = vertices;
  }

  createInfo.indices = indices;
  createInfo.materialIdentifier = to_string(materialId);
  createInfo.position = position;
  createInfo.rotation = rotation;
  createInfo.scale = scale;
  createInfo.visible = true;

  Object *obj = objectManager->create_object(createInfo);
  if (obj) {
    sceneObjects.push_back(obj);
  }
  return obj;
}

void render::Scene::create_basic_material(MaterialId materialId, bool is2D,
                                          bool is3DTextured) {
  // Check if material already exists
  if (materialManager->get_material(to_string(materialId))) {
    return;
  }

  vk::DescriptorSetLayoutBinding uboBinding = {
      .binding = 0,
      .descriptorType = vk::DescriptorType::eUniformBuffer,
      .descriptorCount = 1,
      .stageFlags = vk::ShaderStageFlagBits::eVertex};

  vk::PipelineColorBlendAttachmentState colorBlendAttachment{
      .blendEnable = vk::False,
      .colorWriteMask =
          vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

  vk::VertexInputBindingDescription bindingDescription;
  std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;

  if (is3DTextured) {
    bindingDescription = Object::Vertex3DTextured::getBindingDescription();
    auto attrs = Object::Vertex3DTextured::getAttributeDescriptions();
    attributeDescriptions.assign(attrs.begin(), attrs.end());
  } else {
    bindingDescription = Object::Vertex3D::getBindingDescription();
    auto attrs = Object::Vertex3D::getAttributeDescriptions();
    attributeDescriptions.assign(attrs.begin(), attrs.end());
  }

  Material::MaterialCreateInfo createInfo{
      .identifier = to_string(materialId),
      .vertexShaders = "../assets/shaders/slang.spv",
      .fragmentShaders = "../assets/shaders/slang.spv",
      .descriptorBindings = {uboBinding},
      .rasterizationState = {.depthClampEnable = is2D ? vk::False : vk::True,
                             .rasterizerDiscardEnable = vk::False,
                             .polygonMode = vk::PolygonMode::eFill,
                             .cullMode = is2D ? vk::CullModeFlagBits::eNone
                                              : vk::CullModeFlagBits::eBack,
                             .frontFace = vk::FrontFace::eCounterClockwise,
                             .depthBiasEnable = vk::False,
                             .depthBiasSlopeFactor = 1.0f,
                             .lineWidth = 1.0f},
      .depthStencilState =
          is2D ? vk::PipelineDepthStencilStateCreateInfo{.depthTestEnable =
                                                             vk::False,
                                                         .depthWriteEnable =
                                                             vk::False}
               : vk::PipelineDepthStencilStateCreateInfo{},
      .blendState = {.logicOpEnable = vk::False,
                     .logicOp = vk::LogicOp::eCopy,
                     .attachmentCount = 1,
                     .pAttachments = &colorBlendAttachment},
      .vertexInputState{.vertexBindingDescriptionCount = 1,
                        .pVertexBindingDescriptions = &bindingDescription,
                        .vertexAttributeDescriptionCount =
                            static_cast<uint32_t>(attributeDescriptions.size()),
                        .pVertexAttributeDescriptions =
                            attributeDescriptions.data()},
      .inputAssemblyState{.topology = vk::PrimitiveTopology::eTriangleList},
      .viewportState{.viewportCount = 1, .scissorCount = 1},
      .multisampleState{.rasterizationSamples = vk::SampleCountFlagBits::e1,
                        .sampleShadingEnable = vk::False},
      .dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor}};

  materialManager->add_material(createInfo);

  // Create associated UBO buffer
  device::Buffer::TransformUBO uboData = {.model = glm::mat4(1.0f),
                                          .view = glm::mat4(1.0f),
                                          .proj = glm::mat4(1.0f)};

  device::Buffer::BufferCreateInfo uboInfo = {
      .identifier = to_string(materialId) + "_ubo",
      .type = device::Buffer::BufferType::UNIFORM,
      .usage = device::Buffer::BufferUsage::DYNAMIC,
      .size = sizeof(device::Buffer::TransformUBO),
      .elementSize = sizeof(device::Buffer::TransformUBO),
      .initialData = &uboData};

  bufferManager->create_buffer(uboInfo);
}

void render::Scene::create_textured_material(MaterialId materialId, bool is2D) {
  // Check if material already exists
  if (materialManager->get_material(to_string(materialId))) {
    return;
  }

  vk::DescriptorSetLayoutBinding uboBinding = {
      .binding = 0,
      .descriptorType = vk::DescriptorType::eUniformBuffer,
      .descriptorCount = 1,
      .stageFlags = vk::ShaderStageFlagBits::eVertex};

  vk::DescriptorSetLayoutBinding samplerBinding = {
      .binding = 1,
      .descriptorType = vk::DescriptorType::eCombinedImageSampler,
      .descriptorCount = 1,
      .stageFlags = vk::ShaderStageFlagBits::eFragment};

  vk::VertexInputBindingDescription bindingDescription;
  std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;

  if (is2D) {
    bindingDescription = Object::Vertex2DTextured::getBindingDescription();
    auto attrs = Object::Vertex2DTextured::getAttributeDescriptions();
    attributeDescriptions.assign(attrs.begin(), attrs.end());
  } else {
    bindingDescription = Object::Vertex3DTextured::getBindingDescription();
    auto attrs = Object::Vertex3DTextured::getAttributeDescriptions();
    attributeDescriptions.assign(attrs.begin(), attrs.end());
  }

  vk::PipelineColorBlendAttachmentState colorBlendAttachment{
      .blendEnable = vk::False,
      .colorWriteMask =
          vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

  Material::MaterialCreateInfo createInfo{
      .identifier = to_string(materialId),
      .vertexShaders = "../assets/shaders/textured.spv",
      .fragmentShaders = "../assets/shaders/textured.spv",
      .descriptorBindings = {uboBinding, samplerBinding},
      .rasterizationState = {.depthClampEnable = is2D ? vk::False : vk::True,
                             .rasterizerDiscardEnable = vk::False,
                             .polygonMode = vk::PolygonMode::eFill,
                             .cullMode = vk::CullModeFlagBits::eBack,
                             .frontFace = vk::FrontFace::eCounterClockwise,
                             .depthBiasEnable = vk::False,
                             .depthBiasSlopeFactor = 1.0f,
                             .lineWidth = 1.0f},
      .depthStencilState =
          is2D ? vk::PipelineDepthStencilStateCreateInfo{.depthTestEnable =
                                                             vk::False,
                                                         .depthWriteEnable =
                                                             vk::False}
               : vk::PipelineDepthStencilStateCreateInfo{},
      .blendState = {.logicOpEnable = vk::False,
                     .logicOp = vk::LogicOp::eCopy,
                     .attachmentCount = 1,
                     .pAttachments = &colorBlendAttachment},
      .vertexInputState{.vertexBindingDescriptionCount = 1,
                        .pVertexBindingDescriptions = &bindingDescription,
                        .vertexAttributeDescriptionCount =
                            static_cast<uint32_t>(attributeDescriptions.size()),
                        .pVertexAttributeDescriptions =
                            attributeDescriptions.data()},
      .inputAssemblyState{.topology = vk::PrimitiveTopology::eTriangleList},
      .viewportState{.viewportCount = 1, .scissorCount = 1},
      .multisampleState{.rasterizationSamples = vk::SampleCountFlagBits::e1,
                        .sampleShadingEnable = vk::False},
      .dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor}};

  materialManager->add_material(createInfo);

  // Create associated UBO buffer
  device::Buffer::TransformUBO uboData = {.model = glm::mat4(1.0f),
                                          .view = glm::mat4(1.0f),
                                          .proj = glm::mat4(1.0f)};

  device::Buffer::BufferCreateInfo uboInfo = {
      .identifier = to_string(materialId) + "_ubo",
      .type = device::Buffer::BufferType::UNIFORM,
      .usage = device::Buffer::BufferUsage::DYNAMIC,
      .size = sizeof(device::Buffer::TransformUBO),
      .elementSize = sizeof(device::Buffer::TransformUBO),
      .initialData = &uboData};

  bufferManager->create_buffer(uboInfo);
}

void render::Scene::create_texture(TextureId textureId,
                                   const std::string &path) {
  // Check if texture already exists
  if (textureManager->get_texture(to_string(textureId))) {
    return;
  }

  textureManager->create_texture(to_string(textureId), path);
}

void render::Scene::create_texture_atlas(TextureId textureId,
                                         const std::string &path, uint32_t rows,
                                         uint32_t cols) {
  // Check if texture already exists
  if (textureManager->get_texture(to_string(textureId))) {
    return;
  }

  textureManager->create_texture_atlas(to_string(textureId), path, rows, cols);
}

void render::Scene::cleanup() {
  // Wait for GPU to finish using resources before destroying them
  // This prevents validation errors about destroying resources in use
  if (objectManager) {
    objectManager->wait_idle();
  }

  // Remove all scene objects from the object manager
  // This frees GPU resources while keeping the Scene object in RAM
  for (Object *obj : sceneObjects) {
    if (obj && objectManager) {
      objectManager->remove_object(obj->get_identifier());
    }
  }
  sceneObjects.clear();

  std::print("Scene cleanup: GPU resources freed\n");
}

const std::vector<render::Object *> &render::Scene::get_objects() const {
  return sceneObjects;
}
