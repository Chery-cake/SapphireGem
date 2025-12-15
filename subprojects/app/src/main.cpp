#include "window.h"

int main() {

  render::Window win = render::Window(800, 600, "test");

  win.run();

  return 0;
}
