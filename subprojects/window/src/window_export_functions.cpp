#ifdef ENGINE_DEBUG

#include "window_export.h"
#include "window_export_struct.h"
#include "print_compat.h"

extern "C" {
WINDOW_API void lib_on_load(void *data) {
  std::print("[Window] Library loaded!\n");
  (void)data;
}

WINDOW_API void lib_on_unload(void *data) {
  std::print("[Window] Library unloading!\n");
  (void)data;
}

WINDOW_API void lib_on_reload(void *data) {
  std::print("[Window] Library preparing for reload...\n");
  (void)data;
}

WINDOW_API void lib_on_destroy(void *data) {
  std::print("[Window] Library being destroyed\n");
  windowState *state = static_cast<windowState *>(data);
  delete state;
}
}

#endif // ENGINE_DEBUG
