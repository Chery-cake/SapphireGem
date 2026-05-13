#ifndef BINARY_ARCHIVE_H_
#define BINARY_ARCHIVE_H_

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace core {

// ── BinaryWriter ─────────────────────────────────────────────────────────────

/**
 * @brief Sequential binary output stream backed by a @c std::vector<uint8_t>.
 *
 * @code
 *   BinaryWriter w;
 *   serialize(w, myComponent);
 *   std::vector<uint8_t> bytes = w.takeBuffer();
 * @endcode
 */
class BinaryWriter {
public:
    BinaryWriter() = default;

    /// Write @p count raw bytes.
    void writeBytes(const void *src, std::size_t count) {
        const auto *p = static_cast<const uint8_t *>(src);
        buf_.insert(buf_.end(), p, p + count);
    }

    /// Write a trivially-copyable value.
    template <typename T>
    requires std::is_trivially_copyable_v<T>
    void write(const T &value) {
        writeBytes(&value, sizeof(T));
    }

    /// Write a @c std::string as [uint32 length][utf8 bytes].
    void write(const std::string &s) {
        auto len = static_cast<uint32_t>(s.size());
        write(len);
        writeBytes(s.data(), len);
    }

    /// Write a @c std::vector<T> as [uint32 count][T × count].
    template <typename T>
    requires std::is_trivially_copyable_v<T>
    void writeVector(const std::vector<T> &v) {
        auto count = static_cast<uint32_t>(v.size());
        write(count);
        if (count > 0) {
            writeBytes(v.data(), count * sizeof(T));
        }
    }

    /// The accumulated buffer (read-only view).
    [[nodiscard]] const std::vector<uint8_t> &buffer() const & { return buf_; }

    /// Move the buffer out.
    [[nodiscard]] std::vector<uint8_t> takeBuffer() && { return std::move(buf_); }

private:
    std::vector<uint8_t> buf_;
};

// ── BinaryReader ─────────────────────────────────────────────────────────────

/**
 * @brief Sequential binary input stream reading from a byte span.
 *
 * @code
 *   BinaryReader r{bytes.data(), bytes.size()};
 *   MyComponent c;
 *   deserialize(r, c);
 * @endcode
 */
class BinaryReader {
public:
    BinaryReader(const uint8_t *data, std::size_t size)
        : data_(data), size_(size), pos_(0) {}

    explicit BinaryReader(const std::vector<uint8_t> &v)
        : BinaryReader(v.data(), v.size()) {}

    /// Read @p count raw bytes into @p dst.
    void readBytes(void *dst, std::size_t count) {
        if (pos_ + count > size_) {
            throw std::out_of_range("BinaryReader: read past end of buffer");
        }
        std::memcpy(dst, data_ + pos_, count);
        pos_ += count;
    }

    /// Read a trivially-copyable value.
    template <typename T>
    requires std::is_trivially_copyable_v<T>
    T read() {
        T value{};
        readBytes(&value, sizeof(T));
        return value;
    }

    /// Read a @c std::string.
    std::string readString() {
        auto len = read<uint32_t>();
        std::string s(len, '\0');
        readBytes(s.data(), len);
        return s;
    }

    /// Read a @c std::vector<T>.
    template <typename T>
    requires std::is_trivially_copyable_v<T>
    std::vector<T> readVector() {
        auto count = read<uint32_t>();
        std::vector<T> v(count);
        if (count > 0) {
            readBytes(v.data(), count * sizeof(T));
        }
        return v;
    }

    /// Remaining unread bytes.
    [[nodiscard]] std::size_t remaining() const noexcept {
        return size_ - pos_;
    }

    /// Current read position.
    [[nodiscard]] std::size_t position() const noexcept { return pos_; }

private:
    const uint8_t *data_;
    std::size_t    size_;
    std::size_t    pos_;
};

} // namespace core

// ── Serialization overloads for engine components ────────────────────────────
// These are free functions in their respective namespaces so ADL finds them.

// Include GLM headers required by the component types
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

// ── glm::vec3 ────────────────────────────────────────────────────────────────

namespace glm {

inline void serialize(core::BinaryWriter &w, const glm::vec3 &v) {
    w.write(v.x); w.write(v.y); w.write(v.z);
}
inline void deserialize(core::BinaryReader &r, glm::vec3 &v) {
    v.x = r.read<float>(); v.y = r.read<float>(); v.z = r.read<float>();
}

inline void serialize(core::BinaryWriter &w, const glm::mat3 &m) {
    for (int c = 0; c < 3; ++c)
        for (int row = 0; row < 3; ++row)
            w.write(m[c][row]);
}
inline void deserialize(core::BinaryReader &r, glm::mat3 &m) {
    for (int c = 0; c < 3; ++c)
        for (int row = 0; row < 3; ++row)
            m[c][row] = r.read<float>();
}

inline void serialize(core::BinaryWriter &w, const glm::mat4 &m) {
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row)
            w.write(m[c][row]);
}
inline void deserialize(core::BinaryReader &r, glm::mat4 &m) {
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row)
            m[c][row] = r.read<float>();
}

} // namespace glm

// ── ecs::component::object components ────────────────────────────────────────

// Forward-declare the structs in their namespace so we can write overloads
// here without including heavy Vulkan / VMA headers.  Callers that want to
// use these overloads must #include the relevant component headers first.

namespace ecs::component::object {

// BoundingBox (defined in mesh.h)
struct BoundingBox;

inline void serialize(core::BinaryWriter &w, const BoundingBox &b) {
    glm::serialize(w, b.min);
    glm::serialize(w, b.max);
}
inline void deserialize(core::BinaryReader &r, BoundingBox &b) {
    glm::deserialize(r, b.min);
    glm::deserialize(r, b.max);
}

// TransformComponent<3>
template <uint32_t Dim> struct TransformComponent;

template <uint32_t Dim>
inline void serialize(core::BinaryWriter &w, const TransformComponent<Dim> &t) {
    for (uint32_t i = 0; i < Dim; ++i) w.write(t.position[i]);
    for (uint32_t i = 0; i < Dim; ++i) w.write(t.rotation[i]);
    for (uint32_t i = 0; i < Dim; ++i) w.write(t.scale[i]);
}
template <uint32_t Dim>
inline void deserialize(core::BinaryReader &r, TransformComponent<Dim> &t) {
    for (uint32_t i = 0; i < Dim; ++i) t.position[i] = r.read<float>();
    for (uint32_t i = 0; i < Dim; ++i) t.rotation[i] = r.read<float>();
    for (uint32_t i = 0; i < Dim; ++i) t.scale[i]    = r.read<float>();
}

// Mesh — vertex/index data only (no GPU buffers)
struct Mesh;

inline void serialize(core::BinaryWriter &w, const Mesh &m) {
    w.write(m.dimension);
    w.writeVector(m.vertexData);
    w.writeVector(m.indices);
    serialize(w, m.bounds);
    w.write(m.name);
}
inline void deserialize(core::BinaryReader &r, Mesh &m) {
    m.dimension = r.read<uint32_t>();
    m.vertexData = r.readVector<float>();
    m.indices    = r.readVector<uint32_t>();
    deserialize(r, m.bounds);
    m.name = r.readString();
    m.gpuUploaded = false; // GPU buffers must be re-uploaded
}

} // namespace ecs::component::object

// ── ecs::component::HierarchyNode ────────────────────────────────────────────

namespace ecs::component {

struct HierarchyNode;

inline void serialize(core::BinaryWriter &w, const HierarchyNode &n) {
    w.write(n.entityId);
    w.write(n.parentId);
    glm::serialize(w, n.localTransform);
    glm::serialize(w, n.worldTransform);
}
inline void deserialize(core::BinaryReader &r, HierarchyNode &n) {
    n.entityId      = r.read<EntityId>();
    n.parentId      = r.read<EntityId>();
    glm::deserialize(r, n.localTransform);
    glm::deserialize(r, n.worldTransform);
}

} // namespace ecs::component

#endif // BINARY_ARCHIVE_H_
