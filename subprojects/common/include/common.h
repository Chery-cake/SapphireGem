#pragma once

#include <string>
#include <vector>

class Common {

public:
  static bool readFile(const std::string fileName, std::vector<char> &buffer);
};
