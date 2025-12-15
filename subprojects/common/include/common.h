#pragma once

#include <string>
#include <vector>

namespace general {

class Common {

public:
  static bool readFile(const std::string fileName, std::vector<char> &buffer);
};

} // namespace general
