#include "object.h"
#include "vma_allocator.h"
#include "vulkan_device.h"
#include <glm/gtc/matrix_transform.hpp>
#include <print>

namespace window {

// ============================================================================
// buildModelMatrix specializations
// ============================================================================

template <>
glm::mat4 buildModelMatrix<1>(const Transform<1> &t) {
  glm::mat4 model{1.0f};
  model = glm::translate(model, glm::vec3(t.position.x, 0.0f, 0.0f));
  model = glm::scale(model, glm::vec3(t.scale.x, 1.0f, 1.0f));
  return model;
}

template <>
glm::mat4 buildModelMatrix<2>(const Transform<2> &t) {
  glm::mat4 model{1.0f};
  model = glm::translate(model, glm::vec3(t.position, 0.0f));
  model = glm::rotate(model, t.rotation, glm::vec3(0.0f, 0.0f, 1.0f));
  model = glm::scale(model, glm::vec3(t.scale, 1.0f));
  return model;
}

template <>
glm::mat4 buildModelMatrix<3>(const Transform<3> &t) {
  glm::mat4 model{1.0f};
  model = glm::translate(model, t.position);
  model = glm::rotate(model, t.rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
  model = glm::rotate(model, t.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
  model = glm::rotate(model, t.rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
  model = glm::scale(model, t.scale);
  return model;
}

// ============================================================================
// Descriptor set layout creation (shared by all Object dimensions)
// ============================================================================

std::unique_ptr<vk::raii::DescriptorSetLayout>
createObjectDescriptorSetLayout(device::GPUDevice &device, bool hasTexture) {
  // UBO binding at binding 0
  vk::DescriptorSetLayoutBinding uboBinding{
      0, vk::DescriptorType::eUniformBuffer, 1,
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eGeometry |
          vk::ShaderStageFlagBits::eFragment};

  std::vector<vk::DescriptorSetLayoutBinding> bindings = {uboBinding};

  // If material has a texture, add sampler binding
  if (hasTexture) {
    vk::DescriptorSetLayoutBinding samplerBinding{
        1, vk::DescriptorType::eCombinedImageSampler, 1,
        vk::ShaderStageFlagBits::eFragment};
    bindings.push_back(samplerBinding);
  }

  vk::DescriptorSetLayoutCreateInfo layoutInfo{
      {}, static_cast<uint32_t>(bindings.size()), bindings.data()};

  try {
    return std::make_unique<vk::raii::DescriptorSetLayout>(
        device.getRaiiDevice(), layoutInfo);
  } catch (const vk::SystemError &e) {
    std::println(stderr,
                 "[Object] Failed to create descriptor set layout: {}",
                 e.what());
    return nullptr;
  }
}

} // namespace window
