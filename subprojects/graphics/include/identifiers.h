#pragma once

#include <string>

namespace render {

// Strongly-typed enum for material identifiers
// Prevents typos and provides compile-time safety
enum class MaterialId {
  SIMPLE_SHADERS,             // Basic 3D colored material (was "Test")
  SIMPLE_SHADERS_2D,          // Basic 2D colored material (was "Test2D")
  SIMPLE_SHADERS_3D_TEXTURED, // Basic 3D textured material (was
                              // "Test3DTextured")
  TEXTURED,                   // Generic textured material
  TEXTURED_CHECKERBOARD,      // Checkerboard texture material
  TEXTURED_GRADIENT,          // Gradient texture material
  TEXTURED_ATLAS,             // Atlas texture material
  TEXTURED_3D_CHECKERBOARD,   // 3D Checkerboard texture material
  TEXTURED_3D_GRADIENT,       // 3D Gradient texture material
  TEXTURED_3D_ATLAS,          // 3D Atlas texture material
  TEXTURED_3D_ATLAS_0_0,      // 3D Atlas region (0,0) - top-left
  TEXTURED_3D_ATLAS_0_1,      // 3D Atlas region (0,1) - top-right
  TEXTURED_3D_ATLAS_1_0,      // 3D Atlas region (1,0) - bottom-left
  TEXTURED_3D_ATLAS_1_1,      // 3D Atlas region (1,1) - bottom-right

  LAYERED_2D,                 // 2D layered texture with animated background
  LAYERED_3D,                 // 3D layered texture with animated background
  TEXTURED_3D_LAYERED_CUBE_1, // 3D Layered texture material (1 layer)
  TEXTURED_3D_LAYERED_CUBE_2, // 3D Layered texture material (2 layers)
  TEXTURED_3D_LAYERED_CUBE_3, // 3D Layered texture material (3 layers)
  TEXTURED_3D_LAYERED_CUBE_4, // 3D Layered texture material (4 layers)
  TEXTURED_3D_LAYERED_CUBE_5, // 3D Layered texture material (5 layers)

  SCENE5_FACE_0, // Front face shader
  SCENE5_FACE_1, // Back face shader
  SCENE5_FACE_2, // Left face shader
  SCENE5_FACE_3, // Right face shader
  SCENE5_FACE_4, // Top face shader
  SCENE5_FACE_5  // Bottom face shader
};

// Strongly-typed enum for texture identifiers
enum class TextureId {
  CHECKERBOARD,
  GRADIENT,
  ATLAS,
  ATLAS_0_0, // Atlas region (0,0) - top-left
  ATLAS_0_1, // Atlas region (0,1) - top-right
  ATLAS_1_0, // Atlas region (1,0) - bottom-left
  ATLAS_1_1, // Atlas region (1,1) - bottom-right

  LAYERED_QUAD,   // 3-layer texture for quad
  LAYERED_CUBE_0, // No layers (should not be used)
  LAYERED_CUBE_1, // 1 layer
  LAYERED_CUBE_2, // 2 layers
  LAYERED_CUBE_3, // 3 layers
  LAYERED_CUBE_4, // 4 layers
  LAYERED_CUBE_5, // 5 layers

  SCENE5_CIRCLE,
  SCENE5_STAR,
  SCENE5_SQUARE,
  SCENE5_TRIANGLE,
  SCENE5_HEART,
  SCENE5_DIAMOND,
};

// Helper functions to convert enums to strings
std::string to_string(MaterialId id);
std::string to_string(TextureId id);

// Helper functions to convert strings to enums (for backward compatibility)
MaterialId material_id_from_string(const std::string &str);
TextureId texture_id_from_string(const std::string &str);

// Helper function to check if a material needs per-object UBO
// Returns true for materials that require separate transform buffers per object
bool material_needs_per_object_ubo(const std::string &materialIdentifier);

// Helper function to check if a material uses textured vertices
// Returns true for materials that expect Vertex3DTextured or Vertex2DTextured
// format
bool material_uses_textured_vertices(MaterialId id);

// Helper function to check if a material is designed for 2D rendering
// Returns true for 2D materials (which expect Vertex2DTextured or Vertex3D with
// z=0)
bool material_is_2d(MaterialId id);

} // namespace render
