#include "scene.h"
#include "material_manager.h"
#include "object.h"
#include "texture.h"
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
    const glm::vec3 &scale, const std::vector<uint16_t> &customIndices,
    const std::vector<glm::vec3> &vertexColors) {
  // Default colors if not provided (white for all vertices)
  glm::vec3 defaultColor(1.0f, 1.0f, 1.0f);
  glm::vec3 color0 = vertexColors.size() > 0 ? vertexColors[0] : defaultColor;
  glm::vec3 color1 = vertexColors.size() > 1 ? vertexColors[1] : defaultColor;
  glm::vec3 color2 = vertexColors.size() > 2 ? vertexColors[2] : defaultColor;

  // Define a 2D triangle vertices (in NDC space, z=0)
  const std::vector<Object::Vertex3D> vertices = {
      {{0.0f, -0.5f, 0.0f}, color0}, // Top vertex
      {{-0.5f, 0.5f, 0.0f}, color1}, // Bottom left
      {{0.5f, 0.5f, 0.0f}, color2}   // Bottom right
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
    const std::vector<uint16_t> &customIndices,
    const std::vector<glm::vec3> &vertexColors) {
  // Default color if not provided (white)
  glm::vec3 defaultColor(1.0f, 1.0f, 1.0f);

  // Determine if we need textured vertices
  // Use textured vertices if:
  // 1. A texture ID is explicitly provided, OR
  // 2. Submeshes are provided (for multi-material objects), OR
  // 3. The material itself expects textured vertices
  bool useTexture = textureId.has_value() || !submeshes.empty() ||
                    material_uses_textured_vertices(materialId);

  std::vector<uint16_t> indices;

  Object::ObjectCreateInfo createInfo{.identifier = identifier,
                                      .type = Object::ObjectType::OBJECT_2D};

  if (useTexture) {
    // For multi-material or textured quad, use textured vertices
    if (!submeshes.empty()) {
      // Get colors for multi-material quad (8 vertices)
      std::vector<glm::vec3> colors(8, defaultColor);
      for (size_t i = 0; i < std::min(vertexColors.size(), colors.size());
           ++i) {
        colors[i] = vertexColors[i];
      }

      // Multi-material quad split horizontally into sections
      const std::vector<Object::Vertex2DTextured> vertices = {
          // Left half
          {{-0.5f, -0.5f}, {0.0f, 0.0f}, colors[0]},
          {{0.0f, -0.5f}, {1.0f, 0.0f}, colors[1]},
          {{0.0f, 0.5f}, {1.0f, 1.0f}, colors[2]},
          {{-0.5f, 0.5f}, {0.0f, 1.0f}, colors[3]},
          // Right half
          {{0.0f, -0.5f}, {0.0f, 0.0f}, colors[4]},
          {{0.5f, -0.5f}, {1.0f, 0.0f}, colors[5]},
          {{0.5f, 0.5f}, {1.0f, 1.0f}, colors[6]},
          {{0.0f, 0.5f}, {0.0f, 1.0f}, colors[7]}};

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
        submesh.textureIdentifier =
            def.textureId.has_value() ? to_string(def.textureId.value()) : "";
        objectSubmeshes.push_back(submesh);
      }
      createInfo.submeshes = objectSubmeshes;
    } else {
      // Get colors for single textured quad (4 vertices)
      std::vector<glm::vec3> colors(4, defaultColor);
      for (size_t i = 0; i < std::min(vertexColors.size(), colors.size());
           ++i) {
        colors[i] = vertexColors[i];
      }

      // Single textured quad
      const std::vector<Object::Vertex2DTextured> vertices = {
          {{-0.5f, -0.5f}, {0.0f, 0.0f}, colors[0]}, // Bottom-left
          {{0.5f, -0.5f}, {1.0f, 0.0f}, colors[1]},  // Bottom-right
          {{0.5f, 0.5f}, {1.0f, 1.0f}, colors[2]},   // Top-right
          {{-0.5f, 0.5f}, {0.0f, 1.0f}, colors[3]}   // Top-left
      };

      indices = customIndices.empty() ? std::vector<uint16_t>{0, 2, 1, 0, 3, 2}
                                      : customIndices;

      createInfo.vertices = vertices;
    }

    if (textureId.has_value()) {
      createInfo.textureIdentifier = to_string(textureId.value());
    }
  } else {
    // Get colors for non-textured quad (4 vertices)
    std::vector<glm::vec3> colors(4, defaultColor);
    for (size_t i = 0; i < std::min(vertexColors.size(), colors.size()); ++i) {
      colors[i] = vertexColors[i];
    }

    // Non-textured quad with colored vertices
    const std::vector<Object::Vertex3D> vertices = {
        {{-0.5f, -0.5f, 0.0f}, colors[0]}, // Bottom-left
        {{0.5f, -0.5f, 0.0f}, colors[1]},  // Bottom-right
        {{0.5f, 0.5f, 0.0f}, colors[2]},   // Top-right
        {{-0.5f, 0.5f, 0.0f}, colors[3]}   // Top-left
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
    const std::vector<uint16_t> &customIndices, float cubeSize,
    const std::vector<glm::vec3> &vertexColors) {
  const float s = cubeSize * 0.5f; // Half size

  // Default color if not provided (white)
  glm::vec3 defaultColor(1.0f, 1.0f, 1.0f);

  // Validate that we're not using a 2D material with a 3D object
  if (material_is_2d(materialId)) {
    std::print("Warning: Attempting to use 2D material '{}' with 3D cube '{}'. "
               "This will cause rendering issues. Use a 3D material instead.\n",
               to_string(materialId), identifier);
  }

  // Use textured vertices if:
  // 1. A texture ID is explicitly provided, OR
  // 2. Submeshes are provided (for multi-material objects), OR
  // 3. The material itself expects textured vertices (and is 3D)
  bool useTexture = textureId.has_value() || !submeshes.empty() ||
                    (material_uses_textured_vertices(materialId) &&
                     !material_is_2d(materialId));

  std::vector<uint16_t> indices;

  Object::ObjectCreateInfo createInfo{.identifier = identifier,
                                      .type = Object::ObjectType::OBJECT_3D};

  if (useTexture) {
    // Get colors for textured cube (24 vertices - 4 per face)
    std::vector<glm::vec3> colors(24, defaultColor);
    for (size_t i = 0; i < std::min(vertexColors.size(), colors.size()); ++i) {
      colors[i] = vertexColors[i];
    }

    // Textured vertices for cube
    const std::vector<Object::Vertex3DTextured> vertices = {
        // Front face
        {{-s, -s, s}, {0.0f, 0.0f}, colors[0]}, // 0
        {{s, -s, s}, {1.0f, 0.0f}, colors[1]},  // 1
        {{s, s, s}, {1.0f, 1.0f}, colors[2]},   // 2
        {{-s, s, s}, {0.0f, 1.0f}, colors[3]},  // 3

        // Back face
        {{-s, -s, -s}, {0.0f, 0.0f}, colors[4]}, // 4
        {{s, -s, -s}, {1.0f, 0.0f}, colors[5]},  // 5
        {{s, s, -s}, {1.0f, 1.0f}, colors[6]},   // 6
        {{-s, s, -s}, {0.0f, 1.0f}, colors[7]},  // 7

        // Left face
        {{-s, -s, -s}, {0.0f, 0.0f}, colors[8]}, // 8
        {{-s, -s, s}, {1.0f, 0.0f}, colors[9]},  // 9
        {{-s, s, s}, {1.0f, 1.0f}, colors[10]},  // 10
        {{-s, s, -s}, {0.0f, 1.0f}, colors[11]}, // 11

        // Right face
        {{s, -s, -s}, {0.0f, 0.0f}, colors[12]}, // 12
        {{s, -s, s}, {1.0f, 0.0f}, colors[13]},  // 13
        {{s, s, s}, {1.0f, 1.0f}, colors[14]},   // 14
        {{s, s, -s}, {0.0f, 1.0f}, colors[15]},  // 15

        // Top face
        {{-s, s, -s}, {0.0f, 0.0f}, colors[16]}, // 16
        {{s, s, -s}, {1.0f, 0.0f}, colors[17]},  // 17
        {{s, s, s}, {1.0f, 1.0f}, colors[18]},   // 18
        {{-s, s, s}, {0.0f, 1.0f}, colors[19]},  // 19

        // Bottom face
        {{-s, -s, -s}, {0.0f, 0.0f}, colors[20]}, // 20
        {{s, -s, -s}, {1.0f, 0.0f}, colors[21]},  // 21
        {{s, -s, s}, {1.0f, 1.0f}, colors[22]},   // 22
        {{-s, -s, s}, {0.0f, 1.0f}, colors[23]}   // 23
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
        submesh.textureIdentifier =
            def.textureId.has_value() ? to_string(def.textureId.value()) : "";
        objectSubmeshes.push_back(submesh);
      }
      createInfo.submeshes = objectSubmeshes;
    }
    if (textureId.has_value()) {
      createInfo.textureIdentifier = to_string(textureId.value());
    }
  } else {
    // Get colors for non-textured cube (24 vertices - 4 per face)
    std::vector<glm::vec3> colors(24, defaultColor);
    for (size_t i = 0; i < std::min(vertexColors.size(), colors.size()); ++i) {
      colors[i] = vertexColors[i];
    }

    // Non-textured cube with colored vertices
    const std::vector<Object::Vertex3D> vertices = {
        // Front face
        {{-s, -s, s}, colors[0]}, // 0
        {{s, -s, s}, colors[1]},  // 1
        {{s, s, s}, colors[2]},   // 2
        {{-s, s, s}, colors[3]},  // 3

        // Back face
        {{-s, -s, -s}, colors[4]}, // 4
        {{s, -s, -s}, colors[5]},  // 5
        {{s, s, -s}, colors[6]},   // 6
        {{-s, s, -s}, colors[7]},  // 7

        // Left face
        {{-s, -s, -s}, colors[8]}, // 8
        {{-s, -s, s}, colors[9]},  // 9
        {{-s, s, s}, colors[10]},  // 10
        {{-s, s, -s}, colors[11]}, // 11

        // Right face
        {{s, -s, -s}, colors[12]}, // 12
        {{s, -s, s}, colors[13]},  // 13
        {{s, s, s}, colors[14]},   // 14
        {{s, s, -s}, colors[15]},  // 15

        // Top face
        {{-s, s, -s}, colors[16]}, // 16
        {{s, s, -s}, colors[17]},  // 17
        {{s, s, s}, colors[18]},   // 18
        {{-s, s, s}, colors[19]},  // 19

        // Bottom face
        {{-s, -s, -s}, colors[20]}, // 20
        {{s, -s, -s}, colors[21]},  // 21
        {{s, -s, s}, colors[22]},   // 22
        {{-s, -s, s}, colors[23]}   // 23
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

  // Create shader for this material
  std::vector<Shader::ShaderStageInfo> stages = {
      {.type = Shader::ShaderType::VERTEX,
       .filePath = "assets/shaders/shader.slang",
       .entryPoint = "vertMain"},
      {.type = Shader::ShaderType::FRAGMENT,
       .filePath = "assets/shaders/shader.slang",
       .entryPoint = "fragMain"}};

  Shader::ShaderCreateInfo shaderInfo{
      .identifier = to_string(materialId) + "_shader", .stages = stages};

  auto shader = std::make_unique<Shader>(
      materialManager->get_device_manager()->get_all_logical_devices(),
      shaderInfo);

  if (!shader->compile() || !shader->initialize()) {
    std::print(stderr, "Failed to create shader for material {}\n",
               to_string(materialId));
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
      .shader = shader.get(),
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
               : vk::PipelineDepthStencilStateCreateInfo{.depthTestEnable =
                                                             vk::True,
                                                         .depthWriteEnable =
                                                             vk::True,
                                                         .depthCompareOp = vk::
                                                             CompareOp::eLess},
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

  // Store shader for lifecycle management
  sceneShaders.push_back(std::move(shader));

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

  // Create shader for this material
  std::string shaderPath = is2D ? "assets/shaders/textured.slang"
                                : "assets/shaders/textured3d.slang";

  std::vector<Shader::ShaderStageInfo> stages = {
      {.type = Shader::ShaderType::VERTEX,
       .filePath = shaderPath,
       .entryPoint = "vertMain"},
      {.type = Shader::ShaderType::FRAGMENT,
       .filePath = shaderPath,
       .entryPoint = "fragMain"}};

  Shader::ShaderCreateInfo shaderInfo{
      .identifier = to_string(materialId) + "_shader", .stages = stages};

  auto shader = std::make_unique<Shader>(
      materialManager->get_device_manager()->get_all_logical_devices(),
      shaderInfo);

  if (!shader->compile() || !shader->initialize()) {
    std::print(stderr, "Failed to create shader for material {}\n",
               to_string(materialId));
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
      .shader = shader.get(),
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
               : vk::PipelineDepthStencilStateCreateInfo{.depthTestEnable =
                                                             vk::True,
                                                         .depthWriteEnable =
                                                             vk::True,
                                                         .depthCompareOp = vk::
                                                             CompareOp::eLess},
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

  // Store shader for lifecycle management
  sceneShaders.push_back(std::move(shader));

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

void render::Scene::create_geometry_material(MaterialId materialId, bool is2D, bool isTextured) {
  // Check if material already exists
  if (materialManager->get_material(to_string(materialId))) {
    return;
  }

  // Select shader path based on type
  std::string shaderPath;
  if (is2D && isTextured) {
    shaderPath = "assets/shaders/geometry2d_textured.slang";
  } else if (is2D && !isTextured) {
    shaderPath = "assets/shaders/geometry2d.slang";
  } else if (!is2D && isTextured) {
    shaderPath = "assets/shaders/geometry3d_textured.slang";
  } else {
    shaderPath = "assets/shaders/geometry3d.slang";
  }

  // Geometry shader pipeline: vertex, geometry, and fragment shaders
  std::vector<Shader::ShaderStageInfo> stages = {
      {.type = Shader::ShaderType::VERTEX,
       .filePath = shaderPath,
       .entryPoint = "vertMain"},
      {.type = Shader::ShaderType::GEOMETRY,
       .filePath = shaderPath,
       .entryPoint = "geomMain"},
      {.type = Shader::ShaderType::FRAGMENT,
       .filePath = shaderPath,
       .entryPoint = "fragMain"}};

  Shader::ShaderCreateInfo shaderInfo{
      .identifier = to_string(materialId) + "_shader", .stages = stages};

  auto shader = std::make_unique<Shader>(
      materialManager->get_device_manager()->get_all_logical_devices(),
      shaderInfo);

  if (!shader->compile() || !shader->initialize()) {
    std::print(stderr, "Failed to create geometry shader for material {}\n",
               to_string(materialId));
    return;
  }

  // Setup descriptor bindings
  vk::DescriptorSetLayoutBinding uboBinding = {
      .binding = 0,
      .descriptorType = vk::DescriptorType::eUniformBuffer,
      .descriptorCount = 1,
      .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eGeometry};

  std::vector<vk::DescriptorSetLayoutBinding> bindings = {uboBinding};

  if (isTextured) {
    vk::DescriptorSetLayoutBinding samplerBinding = {
        .binding = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment};
    bindings.push_back(samplerBinding);
  }

  // Vertex input - just points with position and color (minimal data)
  vk::VertexInputBindingDescription bindingDescription;
  std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;

  if (is2D) {
    bindingDescription = Object::Vertex2D::getBindingDescription();
    auto attrs = Object::Vertex2D::getAttributeDescriptions();
    attributeDescriptions.assign(attrs.begin(), attrs.end());
  } else {
    bindingDescription = Object::Vertex3D::getBindingDescription();
    auto attrs = Object::Vertex3D::getAttributeDescriptions();
    attributeDescriptions.assign(attrs.begin(), attrs.end());
  }

  vk::PipelineColorBlendAttachmentState colorBlendAttachment{
      .blendEnable = vk::False,
      .colorWriteMask =
          vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

  Material::MaterialCreateInfo createInfo{
      .identifier = to_string(materialId),
      .shader = shader.get(),
      .descriptorBindings = bindings,
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
               : vk::PipelineDepthStencilStateCreateInfo{.depthTestEnable =
                                                             vk::True,
                                                         .depthWriteEnable =
                                                             vk::True,
                                                         .depthCompareOp = vk::
                                                             CompareOp::eLess},
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
      .inputAssemblyState{.topology = vk::PrimitiveTopology::ePointList},  // Points for geometry shader
      .viewportState{.viewportCount = 1, .scissorCount = 1},
      .multisampleState{.rasterizationSamples = vk::SampleCountFlagBits::e1,
                        .sampleShadingEnable = vk::False},
      .dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor}};

  // Store shader for lifecycle management
  sceneShaders.push_back(std::move(shader));

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

void render::Scene::create_mesh_material(MaterialId materialId, bool is2D) {
  // Check if material already exists
  if (materialManager->get_material(to_string(materialId))) {
    return;
  }

  // Select shader path
  std::string shaderPath = is2D ? "assets/shaders/mesh2d.slang"
                                : "assets/shaders/mesh3d.slang";

  // Mesh shader pipeline: task, mesh, and fragment shaders
  std::vector<Shader::ShaderStageInfo> stages = {
      {.type = Shader::ShaderType::TASK,
       .filePath = shaderPath,
       .entryPoint = "taskMain"},
      {.type = Shader::ShaderType::MESH,
       .filePath = shaderPath,
       .entryPoint = "meshMain"},
      {.type = Shader::ShaderType::FRAGMENT,
       .filePath = shaderPath,
       .entryPoint = "fragMain"}};

  Shader::ShaderCreateInfo shaderInfo{
      .identifier = to_string(materialId) + "_shader", .stages = stages};

  auto shader = std::make_unique<Shader>(
      materialManager->get_device_manager()->get_all_logical_devices(),
      shaderInfo);

  if (!shader->compile() || !shader->initialize()) {
    std::print(stderr, "Failed to create mesh shader for material {}\n",
               to_string(materialId));
    return;
  }

  // Mesh shaders have UBO access in mesh stage
  vk::DescriptorSetLayoutBinding uboBinding = {
      .binding = 0,
      .descriptorType = vk::DescriptorType::eUniformBuffer,
      .descriptorCount = 1,
      .stageFlags = vk::ShaderStageFlagBits::eMeshEXT};

  vk::PipelineColorBlendAttachmentState colorBlendAttachment{
      .blendEnable = vk::False,
      .colorWriteMask =
          vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

  Material::MaterialCreateInfo createInfo{
      .identifier = to_string(materialId),
      .shader = shader.get(),
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
               : vk::PipelineDepthStencilStateCreateInfo{.depthTestEnable =
                                                             vk::True,
                                                         .depthWriteEnable =
                                                             vk::True,
                                                         .depthCompareOp = vk::
                                                             CompareOp::eLess},
      .blendState = {.logicOpEnable = vk::False,
                     .logicOp = vk::LogicOp::eCopy,
                     .attachmentCount = 1,
                     .pAttachments = &colorBlendAttachment},
      .vertexInputState{},  // Mesh shaders don't use vertex input state
      .inputAssemblyState{},  // Mesh shaders don't use input assembly state
      .viewportState{.viewportCount = 1, .scissorCount = 1},
      .multisampleState{.rasterizationSamples = vk::SampleCountFlagBits::e1,
                        .sampleShadingEnable = vk::False},
      .dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor}};

  // Store shader for lifecycle management
  sceneShaders.push_back(std::move(shader));

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
  std::string texId = to_string(textureId);

  // Check if texture already exists
  if (textureManager->get_texture(texId)) {
    // Texture exists, track it for cleanup
    sceneTextures.insert(texId);
    return;
  }

  textureManager->create_texture(texId, path);
  sceneTextures.insert(texId);
}

void render::Scene::create_texture_atlas(TextureId textureId,
                                         const std::string &path, uint32_t rows,
                                         uint32_t cols) {
  std::string texId = to_string(textureId);

  // Check if texture already exists
  if (textureManager->get_texture(texId)) {
    // Texture exists, track it for cleanup
    sceneTextures.insert(texId);
    return;
  }

  textureManager->create_texture_atlas(texId, path, rows, cols);
  sceneTextures.insert(texId);
}

void render::Scene::create_atlas_region_texture(TextureId textureId,
                                                TextureId atlasTextureId,
                                                uint32_t row, uint32_t col) {
  std::string texId = to_string(textureId);
  std::string atlasId = to_string(atlasTextureId);

  // Check if texture already exists
  if (textureManager->get_texture(texId)) {
    sceneTextures.insert(texId);
    return;
  }

  // Get the atlas texture
  Texture *atlasTexture = textureManager->get_texture(atlasId);
  if (!atlasTexture) {
    std::print(stderr,
               "Error: Atlas texture '{}' not found for region texture '{}'\n",
               atlasId, texId);
    return;
  }

  // Get the atlas region by name
  std::string regionName =
      "tile_" + std::to_string(row) + "_" + std::to_string(col);
  const Texture::AtlasRegion *region =
      atlasTexture->get_atlas_region(regionName);

  if (!region) {
    std::print(stderr, "Error: Atlas region '{}' not found in atlas '{}'\n",
               regionName, atlasId);
    return;
  }

  // Create texture that will store just this region
  Texture::TextureCreateInfo createInfo{.identifier = texId,
                                        .type = Texture::TextureType::SINGLE,
                                        .imagePath = ""};

  textureManager->create_texture(createInfo);

  Texture *regionTexture = textureManager->get_texture(texId);
  if (regionTexture && atlasTexture->get_image()) {
    auto atlasImage = atlasTexture->get_image();
    const auto &atlasPixels = atlasImage->get_pixel_data();
    uint32_t atlasWidth = atlasImage->get_width();
    uint32_t atlasChannels = atlasImage->get_channels();

    // Calculate pixel coordinates from UV coordinates
    uint32_t startX = static_cast<uint32_t>(region->uvMin.x * atlasWidth);
    uint32_t startY =
        static_cast<uint32_t>(region->uvMin.y * atlasImage->get_height());
    uint32_t regionWidth = region->width;
    uint32_t regionHeight = region->height;

    // Extract the region pixels
    std::vector<unsigned char> regionPixels(regionWidth * regionHeight *
                                            atlasChannels);
    for (uint32_t y = 0; y < regionHeight; ++y) {
      for (uint32_t x = 0; x < regionWidth; ++x) {
        uint32_t srcOffset =
            ((startY + y) * atlasWidth + (startX + x)) * atlasChannels;
        uint32_t dstOffset = (y * regionWidth + x) * atlasChannels;
        for (uint32_t c = 0; c < atlasChannels; ++c) {
          regionPixels[dstOffset + c] = atlasPixels[srcOffset + c];
        }
      }
    }

    // Load the extracted region into the new texture
    regionTexture->get_image()->load_from_memory(
        regionPixels.data(), regionWidth, regionHeight, atlasChannels);

    regionTexture->update_gpu();
  }

  sceneTextures.insert(texId);
}

void render::Scene::create_layered_texture(
    TextureId textureId, const std::vector<std::string> &imagePaths,
    const std::vector<glm::vec4> &tints, const std::vector<float> &rotations) {
  std::string texId = to_string(textureId);

  // Check if texture already exists
  if (textureManager->get_texture(texId)) {
    sceneTextures.insert(texId);
    return;
  }

  // Build layers
  std::vector<Texture::Layer> layers;
  for (size_t i = 0; i < imagePaths.size(); ++i) {
    Texture::Layer layer(imagePaths[i]);

    if (i < tints.size()) {
      layer.tint = tints[i];
    }

    if (i < rotations.size()) {
      layer.rotation = rotations[i];
    }

    layers.push_back(layer);
  }

  textureManager->create_layered_texture(texId, layers);
  sceneTextures.insert(texId);
}

void render::Scene::cleanup() {
  // Wait for GPU to finish using resources before destroying them
  // This prevents validation errors about destroying resources in use
  if (objectManager) {
    objectManager->wait_idle();
  }

  // Reload all scene textures to reset any modifications
  // This ensures textures are in their original state for the next scene
  size_t texturesReloaded = 0;
  for (const std::string &texId : sceneTextures) {
    if (textureManager) {
      Texture *tex = textureManager->get_texture(texId);
      if (tex && tex->reload()) {
        texturesReloaded++;
      }
    }
  }
  sceneTextures.clear();

  // Remove all scene objects from the object manager
  // This frees GPU resources while keeping the Scene object in RAM
  size_t objectsFreed = sceneObjects.size();
  for (Object *obj : sceneObjects) {
    if (obj && objectManager) {
      objectManager->remove_object(obj->get_identifier());
    }
  }
  sceneObjects.clear();

  std::print("Scene cleanup: {} objects freed, {} textures reset\n",
             objectsFreed, texturesReloaded);
}

const std::vector<render::Object *> &render::Scene::get_objects() const {
  return sceneObjects;
}
