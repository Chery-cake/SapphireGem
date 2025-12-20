#pragma once

#include "buffer_manager.h"
#include "material.h"
#include "material_manager.h"
#include "texture_manager.h"
#include <array>
#include <cstdint>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <variant>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace render {

class Object {
public:
  // Vertex structures - moved from Material to Object
  struct Vertex2D {
    glm::vec2 pos;
    glm::vec3 color;

    static vk::VertexInputBindingDescription getBindingDescription();
    static std::array<vk::VertexInputAttributeDescription, 2>
    getAttributeDescriptions();
  };

  struct Vertex2DTextured {
    glm::vec2 pos;
    glm::vec2 texCoord;
    glm::vec3 color;

    static vk::VertexInputBindingDescription getBindingDescription();
    static std::array<vk::VertexInputAttributeDescription, 3>
    getAttributeDescriptions();
  };

  struct Vertex3D {
    glm::vec3 pos;
    glm::vec3 color;

    static vk::VertexInputBindingDescription getBindingDescription();
    static std::array<vk::VertexInputAttributeDescription, 2>
    getAttributeDescriptions();
  };

  struct Vertex3DTextured {
    glm::vec3 pos;
    glm::vec2 texCoord;
    glm::vec3 color;

    static vk::VertexInputBindingDescription getBindingDescription();
    static std::array<vk::VertexInputAttributeDescription, 3>
    getAttributeDescriptions();
  };

  enum class ObjectType { OBJECT_2D, OBJECT_3D };

  enum class RotationMode {
    SHADER_2D,    // GPU shader-based 2D rotation (Z-axis only)
    TRANSFORM_2D, // CPU/GPU 2D rotation (Z-axis only)
    TRANSFORM_3D  // CPU/GPU 3D rotation (X, Y, Z axes)
  };

  // Submesh structure for multi-material support
  struct Submesh {
    uint32_t indexStart;
    uint32_t indexCount;
    std::string materialIdentifier;
    Material *material;
  };

  // Unified ObjectCreateInfo that works for all vertex types
  struct ObjectCreateInfo {
    std::string identifier;
    ObjectType type;  // Must be explicitly specified (OBJECT_2D or OBJECT_3D)

    // Geometry data - use the appropriate vertex type based on your needs
    std::variant<
        std::vector<Vertex2D>,
        std::vector<Vertex2DTextured>,
        std::vector<Vertex3D>,
        std::vector<Vertex3DTextured>
    > vertices;
    std::vector<uint16_t> indices;

    // Material (shared across instances) - serves as default/base material
    std::string materialIdentifier;

    // Texture identifier for textured objects
    std::string textureIdentifier = "";

    // Optional: Multiple materials for different parts (e.g., different faces)
    // When submeshes don't specify a material, the base materialIdentifier is used
    std::vector<Submesh> submeshes;

    // Transform - use vec2 for 2D objects, vec3 for 3D objects
    // For 2D: only x,y are used; z is ignored
    // For 3D: all x,y,z components are used
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

  // Material references (shared, not owned)
  // Base/default material - used as fallback for submeshes without a material
  Material *material;
  std::string materialIdentifier;

  // Multi-material mode (for different faces/parts)
  std::vector<Submesh> submeshes;
  bool useSubmeshes;

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

  // Helper structures
  struct TransformUBO {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
  };

  // Helper methods
  void update_model_matrix();
  void setup_materials_for_submeshes(std::vector<Submesh> &submeshes);
  std::string get_ubo_buffer_name(const std::string &matIdentifier) const;

public:
  Object(const ObjectCreateInfo &createInfo,
         device::BufferManager *bufferManager, MaterialManager *materialManager,
         TextureManager *textureManager = nullptr);
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
