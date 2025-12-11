#pragma once

#include "buffer.h"
#include "material.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

// Forward declarations
class Material;
class BufferManager;
class MaterialManager;

// Type of renderable object
enum class ObjectType { OBJECT_2D, OBJECT_3D };

// Renderable object that encapsulates geometry, material, and transform
class RenderableObject {
public:
  struct CreateInfo {
    std::string identifier;
    ObjectType type = ObjectType::OBJECT_3D;

    // Geometry data
    std::vector<Material::Vertex2D> vertices;
    std::vector<uint16_t> indices;

    // Material (shared across instances)
    std::string materialIdentifier;

    // Transform
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);

    // Visibility
    bool visible = true;
  };

private:
  std::string identifier;
  ObjectType type;

  // Geometry buffers (owned by this object)
  std::string vertexBufferName;
  std::string indexBufferName;
  uint32_t indexCount;

  // Material reference (shared, not owned)
  Material *material;
  std::string materialIdentifier;

  // Transform
  glm::vec3 position;
  glm::vec3 rotation;
  glm::vec3 scale;
  glm::mat4 modelMatrix;
  bool transformDirty;

  // Visibility
  bool visible;

  // Managers (not owned)
  BufferManager *bufferManager;
  MaterialManager *materialManager;

  void update_model_matrix();

public:
  RenderableObject(const CreateInfo &createInfo, BufferManager *bufferMgr,
                   MaterialManager *materialMgr);
  ~RenderableObject();

  // Prevent copying, allow moving
  RenderableObject(const RenderableObject &) = delete;
  RenderableObject &operator=(const RenderableObject &) = delete;
  RenderableObject(RenderableObject &&) = default;
  RenderableObject &operator=(RenderableObject &&) = default;

  // Render this object
  void draw(vk::raii::CommandBuffer &commandBuffer, uint32_t deviceIndex,
            uint32_t frameIndex);

  // Transform methods
  void set_position(const glm::vec3 &pos);
  void set_rotation(const glm::vec3 &rot);
  void set_scale(const glm::vec3 &scl);
  void set_visible(bool vis);

  // Getters
  const std::string &get_identifier() const { return identifier; }
  ObjectType get_type() const { return type; }
  bool is_visible() const { return visible; }
  const glm::mat4 &get_model_matrix();
  Material *get_material() const { return material; }
};
