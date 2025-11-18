#pragma once

#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

class Common {

public:
  static std::vector<char> readFile(const std::string fileName);

  template <typename... Args>
  static void print(const std::string_view str_fmt, Args &&...args) {
    std::cout << std::vformat(str_fmt, std::make_format_args(args...));
  }
  template <typename... Args>
  static void print(std::ostream &stream, const std::string_view str_fmt,
                    Args &&...args) {
    stream << std::vformat(str_fmt, std::make_format_args(args...));
  }
};
