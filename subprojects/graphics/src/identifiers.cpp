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

} // namespace render
