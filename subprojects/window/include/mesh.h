#ifndef MESH_H_
#define MESH_H_

#include "bindless_types.h"
#include "vma_allocator.h"
#include "glm/glm.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ecs::component::object {

/**
 * @brief Axis-aligned bounding box (dimension-agnostic).
 *
 * Unused components are zero for lower-dimensional meshes.
 */
struct BoundingBox {
  glm::vec3 min{0.0f};
  glm::vec3 max{0.0f};
};

/**
 * @brief A single face (triangle) of a mesh.
 *
 * Tracks the index offset and vertex count for draw calls.
 */
struct Face {
  uint32_t faceIndex    = 0;
  uint32_t vertexOffset = 0; ///< First index entry for this face
  uint32_t vertexCount  = 0; ///< Number of vertices in this face
};

/**
 * @brief Dimension-agnostic mesh resource.
 *
 * Vertex data is stored as a flat array of floats.  Every vertex occupies
 * sizeof(device::GPUVertexPosition)/sizeof(float) == 12 floats laid out as:
 *
 *   [x, y, z, w,   r, g, b, pad0,   nx, ny, nz, npad]
 *
 * where w encodes the spatial dimension (2.0f for 2D meshes, 3.0f for 3D).
 * The per-vertex color (r,g,b) is set by the builder; normals default to
 * (0,1,0) and are overwritten by the compute shader at load time.
 *
 * Use the static helper @ref fromVertices to build a Mesh from a typed
 * Vertex<Dim> array.  GPU upload is performed with @ref upload.
 */
struct Mesh {
  /// Flat vertex data: 12 floats per vertex (GPUVertexPosition layout).
  std::vector<float>              vertexData;
  std::vector<uint32_t>           indices;
  uint32_t                        dimension   = 3; ///< Spatial dimension (2 or 3)
  BoundingBox                     bounds;

  // GPU buffers — populated by upload()
  device::AllocatedBuffer         positionBuffer;
  device::AllocatedBuffer         indexBuffer;
  bool                            gpuUploaded = false;

  std::string                     name  = "Mesh";
  std::vector<Face>               faces; ///< Auto-calculated; see calculateFaces()

  // ── Helpers ────────────────────────────────────────────────────────────

  /// Number of floats that represent one vertex (matches GPUVertexPosition).
  static constexpr uint32_t kFloatsPerVertex =
      sizeof(device::GPUVertexPosition) / sizeof(float);

  /// Number of vertices stored in vertexData.
  [[nodiscard]] uint32_t vertexCount() const {
    return static_cast<uint32_t>(vertexData.size() / kFloatsPerVertex);
  }

  /// Populate @ref faces from @ref indices (every 3 indices = 1 face).
  void calculateFaces();

  [[nodiscard]] uint32_t getFaceCount() const {
    return static_cast<uint32_t>(faces.size());
  }

  /**
   * @brief Upload vertex and index data to the GPU.
   * @return true on success, false if data is empty or allocation failed.
   */
  bool upload(device::VMAAllocator &allocator);

  /**
   * @brief Compute an AABB from the currently stored vertexData.
   *
   * Updates @ref bounds in-place.  Should be called after populating
   * vertexData and before GPU upload.
   */
  void computeBounds();

  // ── Builder helpers ────────────────────────────────────────────────────

  /**
   * @brief Build a Mesh from typed CPU-side vertices.
   *
   * Converts Vertex<Dim>::position and Vertex<Dim>::color into the flat
   * GPUVertexPosition layout.  The @p w component of each vertex is set to
   * @p Dim (so the shader can branch on dimension at runtime).
   *
   * @tparam Dim   Spatial dimension (2 or 3).
   * @tparam Vert  Must expose @c position (std::array<float,Dim>) and
   *               @c color (std::array<float,3>).
   */
  template <uint32_t Dim, typename Vert>
  static Mesh fromVertices(const std::vector<Vert> &verts,
                           const std::vector<uint32_t> &idx,
                           const std::string &meshName = "Mesh");
};

} // namespace ecs::component::object

// Implementation details (template bodies)
#include "../src/mesh_impl.hpp"

#endif // MESH_H_
