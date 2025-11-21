#include "material_manager.h"
#include "devices_manager.h"
#include "material.h"
#include <algorithm>
#include <cassert>
#include <memory>
#include <print>

MaterialManager::MaterialManager(DevicesManager *devicesManager)
    : devicesManager(devicesManager) {}

MaterialManager::~MaterialManager() {

  materials.clear();

  std::print("Material Manager destructor executed\n");
}

void MaterialManager::add_material(Material::MaterialCreateInfo createInfo) {
  materials.insert(materials.end(),
                   std::make_unique<Material>(
                       devicesManager->get_all_logical_devices(), createInfo));
}

void MaterialManager::remove_material(std::string identifier) {
  assert(!materials.empty());
  const auto iterator =
      std::ranges::find_if(materials, [identifier](const auto &material) {
        return material->get_identifier() == identifier;
      });
  materials.erase(iterator);
}
