#include "hot_reload.h"
#include <print>
#include <thread>

struct AppContext {
  int counter;
  BumpAllocator *persistentAlloc;  // Never reset
  BumpAllocator *frameAlloc;       // Reset every frame
};

void onLoad(void *userData) {
  auto *ctx = static_cast<AppContext *>(userData);
  std::print("[Callback] Library loaded! Counter: {}\n", ctx->counter);

  // Allocate persistent data (survives across frames)
  if (ctx->persistentAlloc) {
    int *persistent = static_cast<int*>(
      ctx->persistentAlloc->allocate(sizeof(int), alignof(int))
    );
    *persistent = 42;
    std::print("[Callback] Allocated persistent data\n");
  }
}

void onUnload(void *userData) {
  auto *ctx = static_cast<AppContext *>(userData);
  std::print("[Callback] Cleaning up!  Persistent memory used: {} bytes\n",
             ctx->persistentAlloc->bytes_allocated());
  ctx->counter = 0;
}

void onReload(void *userData) {
  auto *ctx = static_cast<AppContext *>(userData);
  ctx->counter++;
  std::print("[Callback] Reload #{}\n", ctx->counter);
}

int main(int argc, char *argv[]) {

  std::print("argc: {}\n", argc);

  for(int i = 0; i < argc; i++){
    std::print("argv[{}]: {}\n", i, argv[i]);
  }

  // Create separate allocators for different lifetimes
  BumpAllocator persistentAlloc(1 * 1024 * 1024);  // 1MB - never reset
  BumpAllocator frameAlloc(1 * 1024 * 1024);        // 1MB - reset every frame

  AppContext ctx{0, &persistentAlloc, &frameAlloc};

  HotReload core("core", "lib/libcored.so", 2 * 1024 * 1024);
  core.setUserData(&ctx);

  core.registerLoadCallback("InitializeResources", onLoad);
  core.registerUnloadCallback("CleanupResources", onUnload);
  core.registerReloadCallback("PrepareReload", onReload);

  core.registerLoadSymbol("lib_on_load");
  core.registerUnloadSymbol("lib_on_unload");
  core.registerReloadSymbol("lib_on_reload");

  if (!core.load()) {
    std::print(stderr, "Failed to load library!\n");
    return 1;
  }

  std::print("\n=== Starting Hot Reload Loop ===\n\n");

  int frame = 0;
  while (true) {
    // Reset frame allocator at start of each iteration
    frameAlloc.reset();

    if (core.checkAndReloadIfNeeded()) {
      std::print(">>> Library reloaded! <<<\n\n");
      // Note: persistentAlloc keeps its data across reloads!
    }

    auto print_func = (void (*)())core.getSymbol("test_print");
    auto change = (int (*)(int))core.getSymbol("change");

    if (print_func) {
      print_func();
    }

    if (change) {
      int sum = change(2);
      std::print("(2+5)%2={}\n", sum);
    }

    // Frame-local allocations (reset every iteration)
    struct TempData {
      int values[10];
      char message[64];
    };

    auto *temp = static_cast<TempData *>(
      frameAlloc.allocate(sizeof(TempData), alignof(TempData)));

    if (temp) {
      for (int i = 0; i < 10; i++) {
        temp->values[i] = i * frame;
      }

      std::print("Frame allocator:  {}/{} bytes (frame {})\n",
                 frameAlloc.bytes_allocated(),
                 frameAlloc.capacity(),
                 frame);
      std::print("Persistent allocator: {}/{} bytes\n",
                 persistentAlloc.bytes_allocated(),
                 persistentAlloc.capacity());
    }

    std::print("\n");
    frame++;
    std::this_thread::sleep_for(std::chrono:: seconds(2));
  }

  return EXIT_SUCCESS;
}
