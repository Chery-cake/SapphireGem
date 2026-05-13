#pragma once
#include "bindless_types.h"
#include "glm/glm.hpp"
#include "mesh.h"
#include "vma_allocator.h"
#include "vulkan/vulkan.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>

namespace ecs::component::object {

// ============================================================================
// Mesh::calculateFaces
// ============================================================================

inline void Mesh::calculateFaces() {
  faces.clear();

  if (indices.empty()) {
    // No indices: treat each set of 3 vertices as one triangular face.
    const uint32_t faceCount = vertexCount() / 3;
    faces.reserve(faceCount);
    for (uint32_t i = 0; i < faceCount; ++i) {
      faces.push_back({i, i * 3, 3});
    }
  } else {
    // Every 3 consecutive indices form one triangular face.
    const uint32_t faceCount = static_cast<uint32_t>(indices.size()) / 3;
    faces.reserve(faceCount);
    for (uint32_t i = 0; i < faceCount; ++i) {
      faces.push_back({i, i * 3, 3});
    }
  }
}

// ============================================================================
// Mesh::computeBounds
// ============================================================================

inline void Mesh::computeBounds() {
  const uint32_t vCount = vertexCount();
  if (vCount == 0) {
    bounds = {};
    return;
  }

  glm::vec3 mn(std::numeric_limits<float>::max());
  glm::vec3 mx(-std::numeric_limits<float>::max());

  for (uint32_t i = 0; i < vCount; ++i) {
    const float *base = vertexData.data() + i * kFloatsPerVertex;
    glm::vec3 p(base[0], base[1], base[2]);
    mn = glm::min(mn, p);
    mx = glm::max(mx, p);
  }

  bounds.min = mn;
  bounds.max = mx;
}

// ============================================================================
// Mesh::upload
// ============================================================================

inline bool Mesh::upload(device::VMAAllocator &allocator) {
  const uint32_t vCount = vertexCount();
  if (vCount == 0) {
    return false;
  }

  const vk::DeviceSize posDataSize =
      vCount * sizeof(device::GPUVertexPosition);

  positionBuffer = allocator.createHostVisibleStorageBuffer(
      posDataSize, name + "_basePos");
  if (!positionBuffer.isValid()) {
    return false;
  }

  void *mapped = positionBuffer.map();
  if (mapped == nullptr) {
    return false;
  }
  std::memcpy(mapped, vertexData.data(), posDataSize);
  positionBuffer.unmap();
  positionBuffer.flush(0, posDataSize);

  // Index buffer (dual-use as SSBO so compute shaders can read it)
  const size_t idxCount = std::max(indices.size(), size_t(1));
  indexBuffer = allocator.createIndexStorageBuffer(
      idxCount * sizeof(uint32_t), name + "_indices");
  if (!indexBuffer.isValid()) {
    return false;
  }

  if (!indices.empty()) {
    mapped = indexBuffer.map();
    if (mapped != nullptr) {
      std::memcpy(mapped, indices.data(),
                  indices.size() * sizeof(uint32_t));
      indexBuffer.unmap();
      indexBuffer.flush(0, indices.size() * sizeof(uint32_t));
    }
  }

  gpuUploaded = true;
  return true;
}

// ============================================================================
// Mesh::fromVertices<Dim, Vert>
// ============================================================================

template <uint32_t Dim, typename Vert>
Mesh Mesh::fromVertices(const std::vector<Vert> &verts,
                        const std::vector<uint32_t> &idx,
                        const std::string &meshName) {
  static_assert(Dim >= 2 && Dim <= 3,
                "Mesh::fromVertices: Dim must be 2 or 3");

  Mesh m;
  m.name      = meshName;
  m.dimension = Dim;
  m.indices   = idx;
  m.vertexData.resize(verts.size() * kFloatsPerVertex, 0.0f);

  for (size_t i = 0; i < verts.size(); ++i) {
    const size_t base = i * kFloatsPerVertex;
    const auto  &v    = verts[i];

    // Position (x, y, z, w) — w encodes dimension
    m.vertexData[base + 0] = (Dim >= 1) ? v.position[0] : 0.0f;
    m.vertexData[base + 1] = (Dim >= 2) ? v.position[1] : 0.0f;
    m.vertexData[base + 2] = (Dim >= 3) ? v.position[2] : 0.0f;
    m.vertexData[base + 3] = static_cast<float>(Dim); // dimension tag

    // Color (r, g, b, pad)
    m.vertexData[base + 4] = v.color[0];
    m.vertexData[base + 5] = v.color[1];
    m.vertexData[base + 6] = v.color[2];
    m.vertexData[base + 7] = 0.0f; // pad0

    // Normal — default; compute shader will overwrite
    m.vertexData[base + 8]  = 0.0f;
    m.vertexData[base + 9]  = 1.0f;
    m.vertexData[base + 10] = 0.0f;
    m.vertexData[base + 11] = 0.0f; // npad
  }

  return m;
}

} // namespace ecs::component::object
