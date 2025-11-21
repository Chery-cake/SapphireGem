#pragma once

#include "devices_manager.h"
#include "material.h"
#include <memory>
#include <string>
#include <vector>

class MaterialManager {

private:
  const DevicesManager *devicesManager;

  std::vector<std::unique_ptr<Material>> materials;

  void initialize_material();

public:
  MaterialManager(DevicesManager *devicesManager);
  ~MaterialManager();

  void add_material(Material::MaterialCreateInfo createInfo);
  void remove_material(std::string identifier);
};
