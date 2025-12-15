#include "material_manager.h"
#include "device_manager.h"
#include "material.h"
#include <algorithm>
#include <cassert>
#include <memory>
#include <print>
#include <string>
#include <vector>

render::MaterialManager::MaterialManager(device::DeviceManager *deviceManager)
    : deviceManager(deviceManager) {}

render::MaterialManager::~MaterialManager() {

  materials.clear();

  std::print("Material Manager destructor executed\n");
}

void render::MaterialManager::add_material(
    Material::MaterialCreateInfo createInfo) {
  materials.insert(materials.end(),
                   std::make_unique<Material>(
                       deviceManager->get_all_logical_devices(), createInfo));
}

void render::MaterialManager::remove_material(std::string identifier) {
  assert(!materials.empty());
  const auto iterator =
      std::ranges::find_if(materials, [identifier](const auto &material) {
        return material->get_identifier() == identifier;
      });
  materials.erase(iterator);
}

void render::MaterialManager::reload_materials() {
  for (const auto &material : materials) {
    material->reinitialize(); // TODO decide to use single or multi-thread here
    //    Tasks::get_instance().add_task([&material]() {
    //    material->initialize();
    //    },                                   BS::pr::high);
  }
}

render::Material *
render::MaterialManager::get_material(const std::string &identifier) const {
  for (const auto &material : materials) {
    if (material->get_identifier() == identifier) {
      return material.get();
    }
  }
  return nullptr;
}

const std::vector<render::Material *> &
render::MaterialManager::get_materials() const {
  static std::vector<Material *> ptrs;
  ptrs.clear();
  for (const auto &material : materials) {
    ptrs.push_back(material.get());
  }
  return ptrs;
}
