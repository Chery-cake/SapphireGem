#pragma once

#include "device_manager.h"
#include "material.h"
#include <memory>
#include <string>
#include <vector>

class MaterialManager {

private:
  const DeviceManager *deviceManager;

  std::vector<std::unique_ptr<Material>> materials;

  void initialize_material();

public:
  MaterialManager(DeviceManager *deviceManager);
  ~MaterialManager();

  void add_material(Material::MaterialCreateInfo createInfo);
  void remove_material(std::string identifier);
  void reload_materials();

  const std::vector<Material *> &get_materials() const;
};
