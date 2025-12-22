#pragma once

#include <string>
#include <format>

namespace render {

/**
 * Standardized naming conventions for objects and materials
 * This ensures consistent naming across the codebase
 */
namespace naming {

/**
 * Object type prefixes for standardized naming
 */
enum class ObjectType {
  TRIANGLE_2D,
  SQUARE_2D,
  CUBE_3D,
  QUAD_2D,
  CUSTOM
};

/**
 * Material type prefixes for standardized naming
 */
enum class MaterialType {
  TEST,
  TEST_2D,
  TEST_3D_TEXTURED,
  TEXTURED,
  TEXTURED_CHECKERBOARD,
  TEXTURED_GRADIENT,
  TEXTURED_ATLAS,
  TEXTURED_3D_CHECKERBOARD,
  TEXTURED_3D_GRADIENT,
  TEXTURED_3D_ATLAS,
  CUSTOM
};

/**
 * Generate a standardized object name
 * Format: <type>_<descriptor>
 * Examples: triangle_main, cube_player, quad_atlas1
 */
inline std::string make_object_name(ObjectType type, const std::string& descriptor) {
  std::string prefix;
  
  switch (type) {
    case ObjectType::TRIANGLE_2D:
      prefix = "triangle";
      break;
    case ObjectType::SQUARE_2D:
      prefix = "square";
      break;
    case ObjectType::CUBE_3D:
      prefix = "cube";
      break;
    case ObjectType::QUAD_2D:
      prefix = "quad";
      break;
    case ObjectType::CUSTOM:
      return descriptor; // For custom names, just return the descriptor
  }
  
  if (descriptor.empty()) {
    return prefix;
  }
  
  return std::format("{}_{}", prefix, descriptor);
}

/**
 * Generate a standardized material name
 * Format: <MaterialType>
 * Examples: Textured_atlas, Test2D, Textured_checkerboard
 */
inline std::string make_material_name(MaterialType type) {
  switch (type) {
    case MaterialType::TEST:
      return "Test";
    case MaterialType::TEST_2D:
      return "Test2D";
    case MaterialType::TEST_3D_TEXTURED:
      return "Test3DTextured";
    case MaterialType::TEXTURED:
      return "Textured";
    case MaterialType::TEXTURED_CHECKERBOARD:
      return "Textured_checkerboard";
    case MaterialType::TEXTURED_GRADIENT:
      return "Textured_gradient";
    case MaterialType::TEXTURED_ATLAS:
      return "Textured_atlas";
    case MaterialType::TEXTURED_3D_CHECKERBOARD:
      return "Textured3D_checkerboard";
    case MaterialType::TEXTURED_3D_GRADIENT:
      return "Textured3D_gradient";
    case MaterialType::TEXTURED_3D_ATLAS:
      return "Textured3D_atlas";
    case MaterialType::CUSTOM:
      return ""; // Custom materials should use the overload below
  }
  
  return ""; // Fallback for unknown types
}

/**
 * Generate a custom material name
 * Use this for custom materials that don't fit standard types
 */
inline std::string make_material_name(const std::string& customName) {
  return customName;
}

/**
 * Generate a standardized buffer name
 * Format: <material>_<object>_<type>
 * Examples: Textured_atlas_quad1_ubo, Test_cube_vertices
 */
inline std::string make_buffer_name(const std::string& materialName, 
                                    const std::string& objectName, 
                                    const std::string& bufferType) {
  return std::format("{}_{}_{}", materialName, objectName, bufferType);
}

/**
 * Generate a standardized texture name
 * Format: <name>
 * Examples: checkerboard, gradient, atlas
 */
inline std::string make_texture_name(const std::string& name) {
  return name;
}

} // namespace naming

} // namespace render
