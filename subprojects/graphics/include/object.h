#pragma once

#include <array>
#include <cstdint>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <map>
#include <string>
#include <variant>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

namespace device {
class LogicalDevice;
class BufferManager;
class Buffer;
}

namespace render {

class Material;
class MaterialManager;
class TextureManager;
class Image;

// Dimension traits for N-dimensional transforms
// Maps dimension count to appropriate GLM vector types
template <unsigned int N> struct DimensionTraits;

template <> struct DimensionTraits<2> {
  using VecType = glm::vec2;
  static constexpr unsigned int Dims = 2;
};

template <> struct DimensionTraits<3> {
  using VecType = glm::vec3;
  static constexpr unsigned int Dims = 3;
};

template <> struct DimensionTraits<4> {
  using VecType = glm::vec4;
  static constexpr unsigned int Dims = 4;
};

// Transform struct templated on dimension count
// All transforms produce a 4x4 model matrix for GPU compatibility
template <unsigned int N> struct Transform {
  using Vec = typename DimensionTraits<N>::VecType;

  Vec position{0.0f};
  Vec rotation{0.0f};
  Vec scale{1.0f};
  glm::mat4 modelMatrix{1.0f};
  bool dirty = true;

  void set_position(const Vec &pos);
  void set_rotation(const Vec &rot);
  void set_scale(const Vec &scl);
  void update_model_matrix();
  const glm::mat4 &get_model_matrix();
};

// Explicit instantiation declarations (defined in object.cpp)
extern template struct Transform<2>;
extern template struct Transform<3>;
extern template struct Transform<4>;

class Object {
public:
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

  // Submesh structure for multi-material support
  struct Submesh {
    uint32_t indexStart;
    uint32_t indexCount;
    std::string materialIdentifier;
    Material *material;
    std::string textureIdentifier; // Optional: submesh-specific texture
  };

  enum class ObjectType { OBJECT_2D, OBJECT_3D };

  enum class RotationMode {
    SHADER_2D,    // GPU shader-based 2D rotation (Z-axis only)
    TRANSFORM_2D, // CPU/GPU 2D rotation (Z-axis only)
    TRANSFORM_3D  // CPU/GPU 3D rotation (X, Y, Z axes)
  };

  // Unified ObjectCreateInfo that works for all vertex types
  struct ObjectCreateInfo {
    std::string identifier;
    ObjectType type;

    // Geometry data
    std::variant<std::vector<Vertex2D>, std::vector<Vertex2DTextured>,
                 std::vector<Vertex3D>, std::vector<Vertex3DTextured>>
        vertices;
    std::vector<uint16_t> indices;

    // Material (shared across instances)
    std::string materialIdentifier;
    std::string textureIdentifier;

    // Optional: Multiple materials for different parts (e.g., different
    // faces) When submeshes don't specify a material, the base
    // materialIdentifier is used
    std::vector<Submesh> submeshes;

    // Transform (using vec3 for backward compatibility in create info)
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
  // Base/default material - used as fallback for submeshes without a material
  Material *material;
  std::string materialIdentifier;

  // Texture identifier for textured objects
  std::string textureIdentifier;

  // Multi-material mode (for different faces/parts)
  std::vector<Submesh> submeshes;
  bool useSubmeshes;

  // Transform - uses dimension-specific Transform struct
  // 2D objects use Transform<2>, 3D objects use Transform<3>
  Transform<2> transform2D;
  Transform<3> transform3D;

  // Visibility
  bool visible;

  // Managers (not owned)
  device::BufferManager *bufferManager;
  MaterialManager *materialManager;
  class TextureManager *textureManager;

  // Per-object descriptor sets (owned by this object)
  // Each object owns its descriptor sets to allow sharing materials
  // Map: material identifier -> device index -> frame descriptor sets
  std::map<std::string, std::vector<vk::raii::DescriptorSets>>
      materialDescriptorSets;
  // Per-object descriptor set layouts (owned by this object)
  // Map: material identifier -> device index -> descriptor set layout
  std::map<std::string, std::vector<vk::raii::DescriptorSetLayout>>
      materialDescriptorLayouts;
  std::vector<device::LogicalDevice *> logicalDevices;

  RotationMode rotationMode;

  void setup_materials_for_submeshes(std::vector<Submesh> &submeshes);
  std::string get_ubo_buffer_name(const std::string &matIdentifier) const;

  void create_descriptor_sets();
  void create_descriptor_sets_for_material(const std::string &matIdentifier);
  void bind_texture_to_descriptor_sets(const std::string &matIdentifier,
                                       Image *image, uint32_t binding,
                                       uint32_t deviceIndex);
  void bind_buffer_to_descriptor_sets(const std::string &matIdentifier,
                                      device::Buffer *buffer, uint32_t binding,
                                      uint32_t deviceIndex);

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
  void rotate(const glm::vec3 &angles); // For 3D rotation
  void rotate(const glm::vec2 &angles); // For 2D rotation

  // Transform methods (3D)
  void set_position(const glm::vec3 &pos);
  void set_rotation(const glm::vec3 &rot);
  void set_scale(const glm::vec3 &scl);

  // Transform methods (2D)
  void set_position(const glm::vec2 &pos);
  void set_rotation(const glm::vec2 &rot);
  void set_scale(const glm::vec2 &scl);

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
