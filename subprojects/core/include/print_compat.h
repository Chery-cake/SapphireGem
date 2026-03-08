// ============================================================================
// print_compat.h - Compatibility shim for C++23 <print>
// ============================================================================
// MinGW GCC 13.x does not ship the <print> header. When it is missing,
// this header provides minimal std::print / std::println implementations
// built on top of <format> (which GCC 13 does support) and <cstdio>.
// ============================================================================
#pragma once

#if __has_include(<print>)
#include <print>
#else

#include <cstdio>
#include <format>
#include <string>

namespace std {

template <typename... Args>
void print(std::format_string<Args...> fmt, Args &&...args) {
  auto s = std::format(fmt, std::forward<Args>(args)...);
  std::fputs(s.c_str(), stdout);
}

template <typename... Args>
void print(FILE *f, std::format_string<Args...> fmt, Args &&...args) {
  auto s = std::format(fmt, std::forward<Args>(args)...);
  std::fputs(s.c_str(), f);
}

template <typename... Args>
void println(std::format_string<Args...> fmt, Args &&...args) {
  auto s = std::format(fmt, std::forward<Args>(args)...);
  std::fputs(s.c_str(), stdout);
  std::fputc('\n', stdout);
}

template <typename... Args>
void println(FILE *f, std::format_string<Args...> fmt, Args &&...args) {
  auto s = std::format(fmt, std::forward<Args>(args)...);
  std::fputs(s.c_str(), f);
  std::fputc('\n', f);
}

inline void println() { std::fputc('\n', stdout); }

} // namespace std

#endif
