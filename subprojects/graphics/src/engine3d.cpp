#include "engine3d.h"
#include <algorithm>
#include <print>

Engine3D::Engine3D(DeviceManager *devMgr, MaterialManager *matMgr,
                   BufferManager *bufMgr)
    : deviceManager(devMgr), materialManager(matMgr), bufferManager(bufMgr) {
  std::print("Engine3D initialized\n");
}

Engine3D::~Engine3D() {
  objects.clear();
  std::print("Engine3D destroyed\n");
}

void Engine3D::set_render_strategy(RenderStrategy strategy) {
  gpuConfig.strategy = strategy;
  std::print("Render strategy set to: {}\n", static_cast<int>(strategy));
}

void Engine3D::set_gpu_config(const MultiGPUConfig &config) {
  gpuConfig = config;
}

RenderableObject *
Engine3D::create_object(const RenderableObject::CreateInfo &createInfo) {
  // Check if object already exists
  if (objects.find(createInfo.identifier) != objects.end()) {
    std::print("Warning: Object '{}' already exists\n", createInfo.identifier);
    return objects[createInfo.identifier].get();
  }

  // Create the object
  auto object = std::make_unique<RenderableObject>(createInfo, bufferManager,
                                                    materialManager);

  // Track material usage
  materialUsageCount[createInfo.materialIdentifier]++;

  std::print("Created {} object '{}' using material '{}' (usage count: {})\n",
             createInfo.type == ObjectType::OBJECT_2D ? "2D" : "3D",
             createInfo.identifier, createInfo.materialIdentifier,
             materialUsageCount[createInfo.materialIdentifier]);

  auto *objPtr = object.get();
  objects[createInfo.identifier] = std::move(object);

  // Rebuild render queue
  rebuild_render_queue();

  return objPtr;
}

void Engine3D::remove_object(const std::string &identifier) {
  auto it = objects.find(identifier);
  if (it == objects.end()) {
    std::print("Warning: Object '{}' not found\n", identifier);
    return;
  }

  // Decrease material usage count
  std::string materialId = it->second->get_material()->get_identifier();
  if (materialUsageCount[materialId] > 0) {
    materialUsageCount[materialId]--;
    if (materialUsageCount[materialId] == 0) {
      materialUsageCount.erase(materialId);
      std::print("Material '{}' no longer in use\n", materialId);
    }
  }

  std::print("Removed object '{}'\n", identifier);
  objects.erase(it);

  // Rebuild render queue
  rebuild_render_queue();
}

RenderableObject *Engine3D::get_object(const std::string &identifier) {
  auto it = objects.find(identifier);
  if (it == objects.end()) {
    return nullptr;
  }
  return it->second.get();
}

void Engine3D::rebuild_render_queue() {
  renderQueue.clear();
  renderQueue.reserve(objects.size());

  for (auto &pair : objects) {
    if (pair.second->is_visible()) {
      renderQueue.push_back(pair.second.get());
    }
  }

  // Sort by material to minimize state changes
  sort_render_queue_by_material();
}

void Engine3D::sort_render_queue_by_material() {
  std::sort(renderQueue.begin(), renderQueue.end(),
            [](const RenderableObject *a, const RenderableObject *b) {
              // Sort by material pointer (groups objects with same material)
              return a->get_material() < b->get_material();
            });
}

void Engine3D::render_all_objects(vk::raii::CommandBuffer &commandBuffer,
                                  uint32_t deviceIndex, uint32_t frameIndex) {
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
