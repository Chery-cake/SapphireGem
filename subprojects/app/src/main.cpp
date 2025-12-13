#include "window.h"
#include "renderer.h"
#include "render_object.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

int main() {

  Window win = Window(800, 600, "SapphireGem - Triangle and Cube");

  win.run();

  return 0;
}
