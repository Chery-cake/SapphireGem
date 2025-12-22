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
inline std::string make_material_name(MaterialType type, const std::string& suffix = "") {
  std::string name;
  
  switch (type) {
    case MaterialType::TEST:
      name = "Test";
      break;
    case MaterialType::TEST_2D:
      name = "Test2D";
      break;
    case MaterialType::TEST_3D_TEXTURED:
      name = "Test3DTextured";
      break;
    case MaterialType::TEXTURED:
      name = "Textured";
      break;
    case MaterialType::TEXTURED_CHECKERBOARD:
      name = "Textured_checkerboard";
      break;
    case MaterialType::TEXTURED_GRADIENT:
      name = "Textured_gradient";
      break;
    case MaterialType::TEXTURED_ATLAS:
      name = "Textured_atlas";
      break;
    case MaterialType::TEXTURED_3D_CHECKERBOARD:
      name = "Textured3D_checkerboard";
      break;
    case MaterialType::TEXTURED_3D_GRADIENT:
      name = "Textured3D_gradient";
      break;
    case MaterialType::TEXTURED_3D_ATLAS:
      name = "Textured3D_atlas";
      break;
    case MaterialType::CUSTOM:
      return suffix;
  }
  
  if (!suffix.empty()) {
    return std::format("{}_{}", name, suffix);
  }
  
  return name;
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
