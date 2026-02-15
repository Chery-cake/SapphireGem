#include "config.h"
#ifdef ENGINE_DEBUG

#include "device_export.h"
#include "device_export_struct.h"
#include <print>

extern "C" {
DEVICE_API void device_lib_on_load(void *data) {
  std::print("[Device] Library loaded!\n");

  deviceState *state = static_cast<deviceState *>(data);
  if (!state) {
    std::print("[Device] No state provided\n");
    return;
  }

  std::print("[Device] Device module singletons ready\n");
}

DEVICE_API void device_lib_on_unload(void *data) {
  std::print("[Device] Library unloading!\n");
  if (!data) {
    std::print("[Device] No state to preserve (already cleaned up)\n");
    return;
  }

  std::print("[Device] Preserving device state for reload...\n");
}

DEVICE_API void device_lib_on_reload(void *data) {
  std::print("[Device] Library preparing for reload...\n");

  deviceState *state = static_cast<deviceState *>(data);
  if (state) {
    std::print("[Device] Saved device pointers for reload\n");
  }
}

DEVICE_API void device_lib_on_destroy(void *data) {
  std::print("[Device] Library being destroyed - cleaning up...\n");

  deviceState *state = static_cast<deviceState *>(data);
  if (state) {
    if (state->shaderManager) {
      state->shaderManager->shutdown();
      delete state->shaderManager;
      state->shaderManager = nullptr;
    }

    if (state->vmaManager) {
      state->vmaManager->shutdown();
      delete state->vmaManager;
      state->vmaManager = nullptr;
    }

    if (state->deviceManager) {
      state->deviceManager->shutdown();
      delete state->deviceManager;
      state->deviceManager = nullptr;
    }

    if (state->instance) {
      state->instance->shutdown();
      delete state->instance;
      state->instance = nullptr;
    }

    delete state;
    std::print("[Device] Device cleanup complete\n");
  }
}
}
#endif
