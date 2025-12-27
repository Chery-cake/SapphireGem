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
  case MaterialId::TEXTURED_3D_ATLAS_0_0:
    return "Textured3D_atlas_0_0";
  case MaterialId::TEXTURED_3D_ATLAS_0_1:
    return "Textured3D_atlas_0_1";
  case MaterialId::TEXTURED_3D_ATLAS_1_0:
    return "Textured3D_atlas_1_0";
  case MaterialId::TEXTURED_3D_ATLAS_1_1:
    return "Textured3D_atlas_1_1";
  case MaterialId::LAYERED_2D:
    return "Layered_2D";
  case MaterialId::LAYERED_3D:
    return "Layered_3D";
  case MaterialId::TEXTURED_3D_LAYERED_CUBE_1:
    return "Textured3D_layered_cube_1";
  case MaterialId::TEXTURED_3D_LAYERED_CUBE_2:
    return "Textured3D_layered_cube_2";
  case MaterialId::TEXTURED_3D_LAYERED_CUBE_3:
    return "Textured3D_layered_cube_3";
  case MaterialId::TEXTURED_3D_LAYERED_CUBE_4:
    return "Textured3D_layered_cube_4";
  case MaterialId::TEXTURED_3D_LAYERED_CUBE_5:
    return "Textured3D_layered_cube_5";
  case MaterialId::SCENE5_FACE_0:
    return "scene5_face_0";
  case MaterialId::SCENE5_FACE_1:
    return "scene5_face_1";
  case MaterialId::SCENE5_FACE_2:
    return "scene5_face_2";
  case MaterialId::SCENE5_FACE_3:
    return "scene5_face_3";
  case MaterialId::SCENE5_FACE_4:
    return "scene5_face_4";
  case MaterialId::SCENE5_FACE_5:
    return "scene5_face_5";
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
  case TextureId::ATLAS_0_0:
    return "atlas_0_0";
  case TextureId::ATLAS_0_1:
    return "atlas_0_1";
  case TextureId::ATLAS_1_0:
    return "atlas_1_0";
  case TextureId::ATLAS_1_1:
    return "atlas_1_1";
  case TextureId::LAYERED_QUAD:
    return "layered_quad";
  case TextureId::LAYERED_CUBE_0:
    return "layered_cube_0";
  case TextureId::LAYERED_CUBE_1:
    return "layered_cube_1";
  case TextureId::LAYERED_CUBE_2:
    return "layered_cube_2";
  case TextureId::LAYERED_CUBE_3:
    return "layered_cube_3";
  case TextureId::LAYERED_CUBE_4:
    return "layered_cube_4";
  case TextureId::LAYERED_CUBE_5:
    return "layered_cube_5";
  case TextureId::SCENE5_CIRCLE:
    return "scene5_circle";
  case TextureId::SCENE5_STAR:
    return "scene5_star";
  case TextureId::SCENE5_SQUARE:
    return "scene5_square";
  case TextureId::SCENE5_TRIANGLE:
    return "scene5_triangle";
  case TextureId::SCENE5_HEART:
    return "scene5_heart";
  case TextureId::SCENE5_DIAMOND:
    return "scene5_diamond";
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
  if (str == "atlas_0_0")
    return TextureId::ATLAS_0_0;
  if (str == "atlas_0_1")
    return TextureId::ATLAS_0_1;
  if (str == "atlas_1_0")
    return TextureId::ATLAS_1_0;
  if (str == "atlas_1_1")
    return TextureId::ATLAS_1_1;

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
  case MaterialId::TEXTURED_3D_ATLAS_0_0:
  case MaterialId::TEXTURED_3D_ATLAS_0_1:
  case MaterialId::TEXTURED_3D_ATLAS_1_0:
  case MaterialId::TEXTURED_3D_ATLAS_1_1:
  case MaterialId::LAYERED_2D:
  case MaterialId::LAYERED_3D:
  case MaterialId::TEXTURED_3D_LAYERED_CUBE_1:
  case MaterialId::TEXTURED_3D_LAYERED_CUBE_2:
  case MaterialId::TEXTURED_3D_LAYERED_CUBE_3:
  case MaterialId::TEXTURED_3D_LAYERED_CUBE_4:
  case MaterialId::TEXTURED_3D_LAYERED_CUBE_5:
  case MaterialId::SCENE5_FACE_0:
  case MaterialId::SCENE5_FACE_1:
  case MaterialId::SCENE5_FACE_2:
  case MaterialId::SCENE5_FACE_3:
  case MaterialId::SCENE5_FACE_4:
  case MaterialId::SCENE5_FACE_5:
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
  case MaterialId::LAYERED_2D:
    return true;

  // 3D materials
  case MaterialId::SIMPLE_SHADERS:
  case MaterialId::SIMPLE_SHADERS_3D_TEXTURED:
  case MaterialId::TEXTURED_3D_CHECKERBOARD:
  case MaterialId::TEXTURED_3D_GRADIENT:
  case MaterialId::TEXTURED_3D_ATLAS:
  case MaterialId::TEXTURED_3D_ATLAS_0_0:
  case MaterialId::TEXTURED_3D_ATLAS_0_1:
  case MaterialId::TEXTURED_3D_ATLAS_1_0:
  case MaterialId::TEXTURED_3D_ATLAS_1_1:
  case MaterialId::LAYERED_3D:
  case MaterialId::TEXTURED_3D_LAYERED_CUBE_1:
  case MaterialId::TEXTURED_3D_LAYERED_CUBE_2:
  case MaterialId::TEXTURED_3D_LAYERED_CUBE_3:
  case MaterialId::TEXTURED_3D_LAYERED_CUBE_4:
  case MaterialId::TEXTURED_3D_LAYERED_CUBE_5:
  case MaterialId::SCENE5_FACE_0:
  case MaterialId::SCENE5_FACE_1:
  case MaterialId::SCENE5_FACE_2:
  case MaterialId::SCENE5_FACE_3:
  case MaterialId::SCENE5_FACE_4:
  case MaterialId::SCENE5_FACE_5:
    return false;

  default:
    return false;
  }
}

} // namespace render
