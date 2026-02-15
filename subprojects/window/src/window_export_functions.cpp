#include "config.h"
#ifdef ENGINE_DEBUG

#include "window_export.h"
#include "window_export_struct.h"
#include <print>

extern "C" {
WINDOW_API void window_lib_on_load(void *data) {
  std::print("[Window] Library loaded!\n");

  windowState *state = static_cast<windowState *>(data);
  if (!state) {
    std::print("[Window] No state provided\n");
    return;
  }

  std::print("[Window] Window module ready\n");
}

WINDOW_API void window_lib_on_unload(void *data) {
  std::print("[Window] Library unloading!\n");
  if (!data) {
    std::print("[Window] No state to preserve (already cleaned up)\n");
    return;
  }

  std::print("[Window] Preserving window state for reload...\n");
}

WINDOW_API void window_lib_on_reload(void *data) {
  std::print("[Window] Library preparing for reload...\n");

  windowState *state = static_cast<windowState *>(data);
  if (state) {
    std::print("[Window] Saved window pointers for reload\n");
  }
}

WINDOW_API void window_lib_on_destroy(void *data) {
  std::print("[Window] Library being destroyed - cleaning up...\n");

  windowState *state = static_cast<windowState *>(data);
  if (state) {
    if (state->windowManager) {
      state->windowManager->shutdown();
      delete state->windowManager;
      state->windowManager = nullptr;
    }

    delete state;
    std::print("[Window] Window cleanup complete\n");
  }
}
}
#endif
