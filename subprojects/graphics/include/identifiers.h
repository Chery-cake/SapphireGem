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
  TEXTURED_3D_ATLAS_1_1       // 3D Atlas region (1,1) - bottom-right
};

// Strongly-typed enum for texture identifiers
enum class TextureId { CHECKERBOARD, GRADIENT, ATLAS };

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
