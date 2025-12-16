#pragma once

#include "buffer_manager.h"
#include "material.h"
#include "material_manager.h"
#include "texture_manager.h"
#include <cstdint>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <string>
#include <vector>

namespace render {

class Object {
public:
  enum class ObjectType { OBJECT_2D, OBJECT_3D };

  enum class RotationMode {
    SHADER_2D,    // GPU shader-based 2D rotation (Z-axis only)
    TRANSFORM_2D, // CPU/GPU 2D rotation (Z-axis only)
    TRANSFORM_3D  // CPU/GPU 3D rotation (X, Y, Z axes)
  };

  struct ObjectCreateInfo {
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

  struct ObjectCreateInfoTextured {
    std::string identifier;
    ObjectType type = ObjectType::OBJECT_3D;

    // Geometry data
    std::vector<Material::Vertex2DTextured> vertices;
    std::vector<uint16_t> indices;

    // Material (shared across instances)
    std::string materialIdentifier;

    // Texture identifier for textured objects
    std::string textureIdentifier;

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

  // Texture identifier for textured objects
  std::string textureIdentifier;

  // Transform
  glm::vec3 position;
  glm::vec3 rotation;
  glm::vec3 scale;
  glm::mat4 modelMatrix;
  bool transformDirty;

  // Visibility
  bool visible;

  // Managers (not owned)
  device::BufferManager *bufferManager;
  MaterialManager *materialManager;
  class TextureManager *textureManager;

  RotationMode rotationMode;

  void update_model_matrix();

public:
  Object(const ObjectCreateInfo createInfo,
         device::BufferManager *bufferManager, MaterialManager *materialManager,
         TextureManager *textureManager = nullptr);
  Object(const ObjectCreateInfoTextured createInfo,
         device::BufferManager *bufferManager, MaterialManager *materialManager,
         TextureManager *textureManager);
  ~Object();

  // Render this object
  void draw(vk::raii::CommandBuffer &commandBuffer, uint32_t deviceIndex,
            uint32_t frameIndex);

  // Rotation functions
  void rotate_2d(float angle);          // For shader-based 2D rotation (Z-axis)
  void rotate(const glm::vec3 &angles); // For 2D/3D rotation

  // Transform methods
  void set_position(const glm::vec3 &pos);
  void set_rotation(const glm::vec3 &rot);
  void set_scale(const glm::vec3 &scl);
  void set_visible(bool vis);
  void set_rotation_mode(RotationMode mode);

  // Getters
  const std::string &get_identifier() const;
  ObjectType get_type() const;
  bool is_visible() const;
  const glm::mat4 &get_model_matrix();
  Material *get_material() const;
  RotationMode get_rotation_mode() const;
};

} // namespace render
