#include "common.h"
#include <fstream>
#include <iostream>
#include <print>
#include <string>
#include <vector>

bool Common::readFile(const std::string fileName, std::vector<char> &buffer) {
  std::ifstream file(fileName, std::ios::ate | std::ios::binary);

  if (!file.is_open()) {
    std::print(std::cerr, "failed to open file: {}\n", fileName);
    return false;
  }

  size_t fileSize = (size_t)file.tellg();
  buffer.resize(fileSize);

  file.seekg(0);
  file.read(buffer.data(), fileSize);
  file.close();

  return true;
}
