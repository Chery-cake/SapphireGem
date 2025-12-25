#pragma once

#include "buffer_manager.h"
#include "identifiers.h"
#include "material_manager.h"
#include "object.h"
#include "object_manager.h"
#include "texture_manager.h"
#include <glm/vec3.hpp>
#include <string>
#include <vector>

namespace render {

// Base class for all scenes
// Manages creation and lifecycle of objects, materials, and textures for a
// scene
class Scene {
protected:
  MaterialManager *materialManager;
  TextureManager *textureManager;
  device::BufferManager *bufferManager;
  ObjectManager *objectManager;

  std::vector<Object *> sceneObjects;

  // Helper methods for creating objects with automatic index generation
  // These methods handle the common patterns of object creation

  // Create a 2D triangle with colored vertices
  Object *create_triangle_2d(const std::string &identifier,
                             MaterialId materialId,
                             const glm::vec3 &position = glm::vec3(0.0f),
                             const glm::vec3 &rotation = glm::vec3(0.0f),
                             const glm::vec3 &scale = glm::vec3(1.0f),
                             const std::vector<uint16_t> &indices = {},
                             const std::vector<glm::vec3> &vertexColors = {});

  // Submesh definition for multi-material objects
  struct SubmeshDef {
    uint32_t indexStart;
    uint32_t indexCount;
    MaterialId materialId;
  };

  // Create a 2D quad/square - supports single material, textured, and
  // multi-material modes Base material is used for areas not covered by
  // submeshes
  Object *
  create_quad_2d(const std::string &identifier, MaterialId materialId,
                 const std::optional<TextureId> &textureId = std::nullopt,
                 const std::vector<SubmeshDef> &submeshes = {},
                 const glm::vec3 &position = glm::vec3(0.0f),
                 const glm::vec3 &rotation = glm::vec3(0.0f),
                 const glm::vec3 &scale = glm::vec3(1.0f),
                 const std::vector<uint16_t> &indices = {},
                 const std::vector<glm::vec3> &vertexColors = {});

  // Create a 3D cube - supports single material, textured, and multi-material
  // modes Base material is used for areas not covered by submeshes
  Object *
  create_cube_3d(const std::string &identifier, MaterialId materialId,
                 const std::optional<TextureId> &textureId = std::nullopt,
                 const std::vector<SubmeshDef> &submeshes = {},
                 const glm::vec3 &position = glm::vec3(0.0f),
                 const glm::vec3 &rotation = glm::vec3(0.0f),
                 const glm::vec3 &scale = glm::vec3(1.0f),
                 const std::vector<uint16_t> &indices = {},
                 float cubeSize = 1.0f,
                 const std::vector<glm::vec3> &vertexColors = {});

  // Create a 3D cube with atlas region UV mapping
  // Each face can map to a different region of a texture atlas (2x2 grid)
  // atlasRegions: array of 6 pairs (row, col) for each face [front, back,
  // left, right, top, bottom]
  Object *create_cube_3d_with_atlas_regions(
      const std::string &identifier, MaterialId materialId,
      const std::optional<TextureId> &textureId = std::nullopt,
      const std::vector<SubmeshDef> &submeshes = {},
      const std::vector<std::pair<int, int>> &atlasRegions = {},
      const glm::vec3 &position = glm::vec3(0.0f),
      const glm::vec3 &rotation = glm::vec3(0.0f),
      const glm::vec3 &scale = glm::vec3(1.0f), float cubeSize = 1.0f,
      const std::vector<glm::vec3> &vertexColors = {});

  // Helper to create a basic material (non-textured)
  void create_basic_material(MaterialId materialId, bool is2D,
                             bool is3DTextured = false);

  // Helper to create a textured material
  void create_textured_material(MaterialId materialId, bool is2D);

  // Helper to create a texture
  void create_texture(TextureId textureId, const std::string &path);

  // Helper to create a texture atlas
  void create_texture_atlas(TextureId textureId, const std::string &path,
                            uint32_t rows, uint32_t cols);

public:
  Scene(MaterialManager *matMgr, TextureManager *texMgr,
        device::BufferManager *bufMgr, ObjectManager *objMgr);
  virtual ~Scene();

  // Setup the scene (create objects, materials, textures)
  virtual void setup() = 0;

  // Cleanup the scene (remove objects from GPU, keep data in RAM)
  virtual void cleanup();

  // Update the scene (animations, transformations)
  virtual void update(float deltaTime, float totalTime) = 0;

  // Get the name of the scene
  virtual std::string get_name() const = 0;

  // Get all objects in the scene
  const std::vector<Object *> &get_objects() const;
};

} // namespace render
