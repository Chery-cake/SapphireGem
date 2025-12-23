#pragma once

#include <string>

namespace render {

// Strongly-typed enum for material identifiers
// Prevents typos and provides compile-time safety
enum class MaterialId {
  SIMPLE_SHADERS,        // Basic 3D colored material (was "Test")
  SIMPLE_SHADERS_2D,     // Basic 2D colored material (was "Test2D")
  SIMPLE_SHADERS_3D_TEXTURED, // Basic 3D textured material (was "Test3DTextured")
  TEXTURED,              // Generic textured material
  TEXTURED_CHECKERBOARD, // Checkerboard texture material
  TEXTURED_GRADIENT,     // Gradient texture material
  TEXTURED_ATLAS,        // Atlas texture material
  TEXTURED_3D_CHECKERBOARD, // 3D Checkerboard texture material
  TEXTURED_3D_GRADIENT,     // 3D Gradient texture material
  TEXTURED_3D_ATLAS         // 3D Atlas texture material
};

// Strongly-typed enum for texture identifiers
enum class TextureId {
  CHECKERBOARD,
  GRADIENT,
  ATLAS
};

// Helper functions to convert enums to strings
std::string to_string(MaterialId id);
std::string to_string(TextureId id);

// Helper functions to convert strings to enums (for backward compatibility)
MaterialId material_id_from_string(const std::string &str);
TextureId texture_id_from_string(const std::string &str);

} // namespace render
