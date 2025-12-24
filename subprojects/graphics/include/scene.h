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
                             const std::vector<uint16_t> &indices = {});

  // Create a 2D quad/square with colored vertices
  Object *create_quad_2d(const std::string &identifier, MaterialId materialId,
                         const glm::vec3 &position = glm::vec3(0.0f),
                         const glm::vec3 &rotation = glm::vec3(0.0f),
                         const glm::vec3 &scale = glm::vec3(1.0f),
                         const std::vector<uint16_t> &indices = {});

  // Create a 2D textured quad/square
  Object *create_textured_quad_2d(const std::string &identifier,
                                  MaterialId materialId, TextureId textureId,
                                  const glm::vec3 &position = glm::vec3(0.0f),
                                  const glm::vec3 &rotation = glm::vec3(0.0f),
                                  const glm::vec3 &scale = glm::vec3(1.0f),
                                  const std::vector<uint16_t> &indices = {});

  // Create a 3D cube with colored vertices (single material)
  Object *create_cube_3d(const std::string &identifier, MaterialId materialId,
                         const glm::vec3 &position = glm::vec3(0.0f),
                         const glm::vec3 &rotation = glm::vec3(0.0f),
                         const glm::vec3 &scale = glm::vec3(1.0f),
                         const std::vector<uint16_t> &indices = {});

  // Create a 3D cube with multiple materials (one per face)
  Object *
  create_multi_material_cube_3d(const std::string &identifier,
                                const std::vector<MaterialId> &faceMaterials,
                                const std::vector<TextureId> &faceTextures = {},
                                const glm::vec3 &position = glm::vec3(0.0f),
                                const glm::vec3 &rotation = glm::vec3(0.0f),
                                const glm::vec3 &scale = glm::vec3(1.0f));

  // Create a 2D quad with multiple materials (e.g., split horizontally)
  Object *
  create_multi_material_quad_2d(const std::string &identifier,
                                const std::vector<MaterialId> &materials,
                                const std::vector<TextureId> &textures = {},
                                const glm::vec3 &position = glm::vec3(0.0f),
                                const glm::vec3 &rotation = glm::vec3(0.0f),
                                const glm::vec3 &scale = glm::vec3(1.0f));

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
