#pragma once

#include "device_manager.h"
#include "material.h"
#include <memory>
#include <string>
#include <vector>

namespace render {

class MaterialManager {

private:
  const device::DeviceManager *deviceManager;

  std::vector<std::unique_ptr<Material>> materials;

  void initialize_material();

public:
  MaterialManager(device::DeviceManager *deviceManager);
  ~MaterialManager();

  void add_material(Material::MaterialCreateInfo createInfo);
  void remove_material(std::string identifier);
  void reload_materials();

  Material *get_material(const std::string &identifier) const;
  const std::vector<Material *> &get_materials() const;
  
  // Access to device manager for descriptor set creation
  const device::DeviceManager *get_device_manager() const;
};

} // namespace render
