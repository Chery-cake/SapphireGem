#include "identifiers.h"
#include <stdexcept>

namespace render {

std::string to_string(MaterialId id) {
  switch (id) {
  case MaterialId::SIMPLE_SHADERS:
    return "simple_shaders";
  case MaterialId::SIMPLE_SHADERS_2D:
    return "simple_shaders_2d";
  case MaterialId::SIMPLE_SHADERS_3D_TEXTURED:
    return "simple_shaders_3d_textured";
  case MaterialId::TEXTURED:
    return "Textured";
  case MaterialId::TEXTURED_CHECKERBOARD:
    return "Textured_checkerboard";
  case MaterialId::TEXTURED_GRADIENT:
    return "Textured_gradient";
  case MaterialId::TEXTURED_ATLAS:
    return "Textured_atlas";
  case MaterialId::TEXTURED_3D_CHECKERBOARD:
    return "Textured3D_checkerboard";
  case MaterialId::TEXTURED_3D_GRADIENT:
    return "Textured3D_gradient";
  case MaterialId::TEXTURED_3D_ATLAS:
    return "Textured3D_atlas";
  default:
    throw std::runtime_error("Unknown MaterialId");
  }
}

std::string to_string(TextureId id) {
  switch (id) {
  case TextureId::CHECKERBOARD:
    return "checkerboard";
  case TextureId::GRADIENT:
    return "gradient";
  case TextureId::ATLAS:
    return "atlas";
  default:
    throw std::runtime_error("Unknown TextureId");
  }
}

MaterialId material_id_from_string(const std::string &str) {
  if (str == "simple_shaders" || str == "Test")
    return MaterialId::SIMPLE_SHADERS;
  if (str == "simple_shaders_2d" || str == "Test2D")
    return MaterialId::SIMPLE_SHADERS_2D;
  if (str == "simple_shaders_3d_textured" || str == "Test3DTextured")
    return MaterialId::SIMPLE_SHADERS_3D_TEXTURED;
  if (str == "Textured")
    return MaterialId::TEXTURED;
  if (str == "Textured_checkerboard")
    return MaterialId::TEXTURED_CHECKERBOARD;
  if (str == "Textured_gradient")
    return MaterialId::TEXTURED_GRADIENT;
  if (str == "Textured_atlas")
    return MaterialId::TEXTURED_ATLAS;
  if (str == "Textured3D_checkerboard")
    return MaterialId::TEXTURED_3D_CHECKERBOARD;
  if (str == "Textured3D_gradient")
    return MaterialId::TEXTURED_3D_GRADIENT;
  if (str == "Textured3D_atlas")
    return MaterialId::TEXTURED_3D_ATLAS;

  throw std::runtime_error("Unknown material identifier: " + str);
}

TextureId texture_id_from_string(const std::string &str) {
  if (str == "checkerboard")
    return TextureId::CHECKERBOARD;
  if (str == "gradient")
    return TextureId::GRADIENT;
  if (str == "atlas")
    return TextureId::ATLAS;

  throw std::runtime_error("Unknown texture identifier: " + str);
}

bool material_needs_per_object_ubo(const std::string &materialIdentifier) {
  // Materials that need per-object UBOs are those with transform uniforms
  // This includes all materials in our current system except for potential
  // future materials that might not need transforms
  //
  // The naming convention is:
  // - Materials starting with "simple_shaders" need UBOs (basic colored
  // materials)
  // - Materials starting with "Textured" or "TEXTURED" need UBOs (textured
  // materials)
  //
  // This function centralizes the logic so adding new materials following
  // these naming conventions automatically works without code changes

  if (materialIdentifier.empty()) {
    return false;
  }

  // Check for simple_shaders prefix (case-sensitive)
  if (materialIdentifier.rfind("simple_shaders", 0) == 0) {
    return true;
  }

  // Check for Textured prefix (handles both "Textured" and "TEXTURED")
  if (materialIdentifier.rfind("Textured", 0) == 0 ||
      materialIdentifier.rfind("TEXTURED", 0) == 0) {
    return true;
  }

  return false;
}

bool material_uses_textured_vertices(MaterialId id) {
  switch (id) {
  // Materials that use textured vertices
  case MaterialId::TEXTURED:
  case MaterialId::TEXTURED_CHECKERBOARD:
  case MaterialId::TEXTURED_GRADIENT:
  case MaterialId::TEXTURED_ATLAS:
  case MaterialId::TEXTURED_3D_CHECKERBOARD:
  case MaterialId::TEXTURED_3D_GRADIENT:
  case MaterialId::TEXTURED_3D_ATLAS:
  case MaterialId::SIMPLE_SHADERS_3D_TEXTURED:
    return true;

  // Materials that use non-textured vertices
  case MaterialId::SIMPLE_SHADERS:
  case MaterialId::SIMPLE_SHADERS_2D:
    return false;

  default:
    return false;
  }
}

bool material_is_2d(MaterialId id) {
  switch (id) {
  // 2D materials
  case MaterialId::SIMPLE_SHADERS_2D:
  case MaterialId::TEXTURED:
  case MaterialId::TEXTURED_CHECKERBOARD:
  case MaterialId::TEXTURED_GRADIENT:
  case MaterialId::TEXTURED_ATLAS:
    return true;

  // 3D materials
  case MaterialId::SIMPLE_SHADERS:
  case MaterialId::SIMPLE_SHADERS_3D_TEXTURED:
  case MaterialId::TEXTURED_3D_CHECKERBOARD:
  case MaterialId::TEXTURED_3D_GRADIENT:
  case MaterialId::TEXTURED_3D_ATLAS:
    return false;

  default:
    return false;
  }
}

} // namespace render
