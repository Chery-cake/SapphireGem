#include "object_manager.h"
#include "buffer_manager.h"
#include "device_manager.h"
#include "material_manager.h"
#include "object.h"
#include "texture_manager.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <print>
#include <string>
#include <vulkan/vulkan_raii.hpp>

render::ObjectManager::ObjectManager(device::DeviceManager *deviceManager,
                                     MaterialManager *materialManager,
                                     device::BufferManager *bufferManager,
                                     TextureManager *textureManager)
    : deviceManager(deviceManager), materialManager(materialManager),
      bufferManager(bufferManager), textureManager(textureManager) {}

render::ObjectManager::~ObjectManager() {
  objects.clear();
  std::print("Object manager destructor executed\n");
}

void render::ObjectManager::rebuild_render_queue() {
  renderQueue.clear();
  renderQueue.reserve(objects.size());

  for (auto &object : objects) {
    if (object.second->is_visible()) {
      renderQueue.push_back(object.second.get());
    }
  }

  // Sort by material to minimize state changes
  sort_render_queue_by_material();
}

void render::ObjectManager::sort_render_queue_by_material() {
  std::sort(renderQueue.begin(), renderQueue.end(),
            [](const Object *a, const Object *b) {
              // Sort by material pointer (groups objects with same
              // material)
              return a->get_material() < b->get_material();
            });
}

void render::ObjectManager::set_render_strategy(
    render::ObjectManager::RenderStrategy strategy) {
  gpuConfig.strategy = strategy;
  std::print("Render strategy set to: {}\n", static_cast<int>(strategy));
}

void render::ObjectManager::set_gpu_config(
    const render::ObjectManager::MultiGPUConfig &config) {
  gpuConfig = config;
}

const render::ObjectManager::MultiGPUConfig &
render::ObjectManager::get_gpu_config() const {
  return gpuConfig;
}

render::Object *render::ObjectManager::create_object(
    const Object::ObjectCreateInfo &createInfo) {
  // Check if object already exists
  if (objects.find(createInfo.identifier) != objects.end()) {
    std::print("Warning: Object '{}' already exists\n", createInfo.identifier);
    return objects[createInfo.identifier].get();
  }

  auto object = std::make_unique<Object>(createInfo, bufferManager,
                                         materialManager, textureManager);

  // Track material usage
  materialUsageCount[createInfo.materialIdentifier]++;

  std::string objTypeStr =
      createInfo.textureIdentifier.empty() ? "" : "textured ";
  std::print("Created {}{} object '{}' using material '{}' (usage count: {})\n",
             objTypeStr,
             createInfo.type == Object::ObjectType::OBJECT_2D ? "2D" : "3D",
             createInfo.identifier, createInfo.materialIdentifier,
             materialUsageCount[createInfo.materialIdentifier]);

  auto *objPtr = object.get();
  objects[createInfo.identifier] = std::move(object);

  // Rebuild render queue
  rebuild_render_queue();

  return objPtr;
}

void render::ObjectManager::remove_object(const std::string &identifier) {
  auto it = objects.find(identifier);
  if (it == objects.end()) {
    std::print("Warning: Object '{}' not found\n", identifier);
    return;
  }

  // Decrease material usage count
  std::string materialId = it->second->get_material()->get_identifier();
  if (materialUsageCount[materialId] > 0) {
    materialUsageCount[materialId]--;
  } else if (materialUsageCount[materialId] == 0) {
    materialUsageCount.erase(materialId);
    std::print("Material '{}' no longer in use\n", materialId);
  }

  std::print("Removed object '{}'\n", identifier);
  objects.erase(it);

  // Rebuild render queue
  rebuild_render_queue();
}

render::Object *
render::ObjectManager::get_object(const std::string &identifier) {
  auto it = objects.find(identifier);
  if (it == objects.end()) {
    return nullptr;
  }
  return it->second.get();
}

void render::ObjectManager::render_all_objects(
    vk::raii::CommandBuffer &commandBuffer, uint32_t deviceIndex,
    uint32_t frameIndex) {
  Material *currentMaterial = nullptr;

  for (auto *object : renderQueue) {
    // Only bind material pipeline if it changed (optimization)
    // Objects will bind their own descriptor sets during draw()
    Material *objMaterial = object->get_material();
    if (objMaterial != currentMaterial) {
      if (objMaterial) {
        // Bind only the pipeline, not descriptor sets (objects manage those)
        objMaterial->bind(commandBuffer, nullptr, deviceIndex);
        currentMaterial = objMaterial;
      }
    }

    // Draw the object (will bind its descriptor sets and issue draw call)
    object->draw(commandBuffer, deviceIndex, frameIndex);
  }
}

size_t render::ObjectManager::get_object_count() const {
  return objects.size();
}

size_t render::ObjectManager::get_material_count() const {
  return materialUsageCount.size();
}
