#include <cstdlib>
#include <print>

int main(int argc, char *argv[]) {

  std::print("Test: {}\n", argc);

  for (int i = 0; i < argc; i++) {
    std::print("{}: {}\n", i, argv[i]);
  }

  return EXIT_SUCCESS;
}
