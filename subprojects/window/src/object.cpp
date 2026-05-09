#include "object.h"
#include <algorithm>
#include <cstdint>
#include <ranges>

namespace ecs::component::object {

template <>
typename TransformComponent<1>::MatType
TransformComponent<1>::modelMatrix() const {
  constexpr uint32_t N = 1 + 1;
  MatType result(1.0f);

  std::ranges::for_each(std::views::iota(0u, scale.size()),
                        [&](uint32_t i) { result[i][i] = scale[i]; });

  std::ranges::for_each(std::views::iota(0u, position.size()),
                        [&](uint32_t i) { result[N - 1][i] = position[i]; });

  return result;
}

template <>
typename TransformComponent<2>::MatType
TransformComponent<2>::modelMatrix() const {
  constexpr uint32_t N = 2 + 1;
  MatType result(1.0f);

  std::ranges::for_each(std::views::iota(0u, scale.size()),
                        [&](uint32_t i) { result[i][i] = scale[i]; });

  float angle = rotation[0];

  if (angle != 0.0f) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    MatType rot(1.0f);

    rot[0][0] = c;
    rot[0][1] = -s;

    rot[1][0] = s;
    rot[1][1] = c;

    result *= rot;
  }

  std::ranges::for_each(std::views::iota(0u, position.size()),
                        [&](uint32_t i) { result[N - 1][i] = position[i]; });

  return result;
}

template <>
typename TransformComponent<3>::MatType
TransformComponent<3>::modelMatrix() const {
  constexpr uint32_t N = 3 + 1;
  MatType result(1.0f);

  std::ranges::for_each(std::views::iota(0u, scale.size()),
                        [&](uint32_t i) { result[i][i] = scale[i]; });

  // 3D standard right‑handed axis‑angle rotations
  // rotation[0] = around X   (plane YZ)
  // rotation[1] = around Y   (plane XZ)
  // rotation[2] = around Z   (plane XY)

  using FillFn = void (*)(MatType &rot, float c, float s);

  static constexpr FillFn fill[3] = {// X
                                     +[](MatType &rot, float c, float s) {
                                       rot[1][1] = c;
                                       rot[1][2] = -s;
                                       rot[2][1] = s;
                                       rot[2][2] = c;
                                     },
                                     // Y
                                     +[](MatType &rot, float c, float s) {
                                       rot[0][0] = c;
                                       rot[0][2] = s;
                                       rot[2][0] = -s;
                                       rot[2][2] = c;
                                     },
                                     // Z
                                     +[](MatType &rot, float c, float s) {
                                       rot[0][0] = c;
                                       rot[0][1] = -s;
                                       rot[1][0] = s;
                                       rot[1][1] = c;
                                     }};

  std::ranges::for_each(std::views::iota(0u, rotation.size()), [&](uint32_t i) {
    float angle = rotation[i];
    if (angle == 0.0f) {
      return;
    }
    float c = std::cos(angle);
    float s = std::sin(angle);
    MatType rot(1.0f);
    fill[i](rot, c, s);
    result = rot * result;
  });

  std::ranges::for_each(std::views::iota(0u, position.size()),
                        [&](uint32_t i) { result[N - 1][i] = position[i]; });

  return result;
}

} // namespace ecs::component::object
