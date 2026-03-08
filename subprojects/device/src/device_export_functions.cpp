#ifdef ENGINE_DEBUG

#include "device_export.h"
#include "device_export_struct.h"
#include "print_compat.h"

extern "C" {
DEVICE_API void lib_on_load(void *data) {
  std::print("[Device] Library loaded!\n");
  (void)data;
}

DEVICE_API void lib_on_unload(void *data) {
  std::print("[Device] Library unloading!\n");
  (void)data;
}

DEVICE_API void lib_on_reload(void *data) {
  std::print("[Device] Library preparing for reload...\n");
  (void)data;
}

DEVICE_API void lib_on_destroy(void *data) {
  std::print("[Device] Library being destroyed\n");
  deviceState *state = static_cast<deviceState *>(data);
  delete state;
}
}

#endif // ENGINE_DEBUG
