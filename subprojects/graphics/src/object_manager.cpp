#include "object_manager.h"
#include "buffer_manager.h"
#include "device_manager.h"
#include "material_manager.h"
#include "render_object.h"
#include "texture_manager.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <print>
#include <string>
#include <vulkan/vulkan_raii.hpp>

ObjectManager::ObjectManager(DeviceManager *deviceManager,
                             MaterialManager *materialManager,
                             BufferManager *bufferManager,
                             TextureManager *textureManager)
    : deviceManager(deviceManager), materialManager(materialManager),
      bufferManager(bufferManager), textureManager(textureManager) {}

ObjectManager::~ObjectManager() {
  objects.clear();
  std::print("Object manager destructor executed\n");
}

void ObjectManager::rebuild_render_queue() {
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

void ObjectManager::sort_render_queue_by_material() {
  std::sort(renderQueue.begin(), renderQueue.end(),
            [](const RenderObject *a, const RenderObject *b) {
              // Sort by material pointer (groups objects with same
              // material)
              return a->get_material() < b->get_material();
            });
}

void ObjectManager::set_render_strategy(
    ObjectManager::RenderStrategy strategy) {
  gpuConfig.strategy = strategy;
  std::print("Render strategy set to: {}\n", static_cast<int>(strategy));
}

void ObjectManager::set_gpu_config(
    const ObjectManager::MultiGPUConfig &config) {
  gpuConfig = config;
}

const ObjectManager::MultiGPUConfig &ObjectManager::get_gpu_config() const {
  return gpuConfig;
}

RenderObject *
ObjectManager::create_object(const RenderObject::ObjectCreateInfo &createInfo) {
  // Check if object already exists
  if (objects.find(createInfo.identifier) != objects.end()) {
    std::print("Warning: Object '{}' already exists\n", createInfo.identifier);
    return objects[createInfo.identifier].get();
  }

  auto object = std::make_unique<RenderObject>(createInfo, bufferManager,
                                               materialManager);

  // Track material usage
  materialUsageCount[createInfo.materialIdentifier]++;

  std::print("Created {} object '{}' using material '{}' (usage count: {})\n",
             createInfo.type == RenderObject::ObjectType::OBJECT_2D ? "2D"
                                                                    : "3D",
             createInfo.identifier, createInfo.materialIdentifier,
             materialUsageCount[createInfo.materialIdentifier]);

  auto *objPtr = object.get();
  objects[createInfo.identifier] = std::move(object);

  // Rebuild render queue
  rebuild_render_queue();

  return objPtr;
}

RenderObject *ObjectManager::create_textured_object(
    const RenderObject::ObjectCreateInfoTextured &createInfo) {
  // Check if object already exists
  if (objects.find(createInfo.identifier) != objects.end()) {
    std::print("Warning: Object '{}' already exists\n", createInfo.identifier);
    return objects[createInfo.identifier].get();
  }

  auto object = std::make_unique<RenderObject>(createInfo, bufferManager,
                                               materialManager, textureManager);

  // Track material usage
  materialUsageCount[createInfo.materialIdentifier]++;

  std::print("Created textured {} object '{}' using material '{}' (usage "
             "count: {})\n",
             createInfo.type == RenderObject::ObjectType::OBJECT_2D ? "2D"
                                                                    : "3D",
             createInfo.identifier, createInfo.materialIdentifier,
             materialUsageCount[createInfo.materialIdentifier]);

  auto *objPtr = object.get();
  objects[createInfo.identifier] = std::move(object);

  // Rebuild render queue
  rebuild_render_queue();

  return objPtr;
}

void ObjectManager::remove_object(const std::string &identifier) {
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

RenderObject *ObjectManager::get_object(const std::string &identifier) {
  auto it = objects.find(identifier);
  if (it == objects.end()) {
    return nullptr;
  }
  return it->second.get();
}

void ObjectManager::render_all_objects(vk::raii::CommandBuffer &commandBuffer,
                                       uint32_t deviceIndex,
                                       uint32_t frameIndex) {
  Material *currentMaterial = nullptr;

  for (auto *object : renderQueue) {
    // Only bind material if it changed (optimization)
    Material *objMaterial = object->get_material();
    if (objMaterial != currentMaterial) {
      if (objMaterial) {
        objMaterial->bind(commandBuffer, deviceIndex, frameIndex);
        currentMaterial = objMaterial;
      }
    }

    // Draw the object (will bind its buffers and issue draw call)
    object->draw(commandBuffer, deviceIndex, frameIndex);
  }
}

size_t ObjectManager::get_object_count() const { return objects.size(); }

size_t ObjectManager::get_material_count() const {
  return materialUsageCount.size();
}
