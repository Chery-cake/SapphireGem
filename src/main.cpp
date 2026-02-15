#include "renderer.h"
#include "shader_manager.h"
#include "swapchain.h"
#include "thread_manager.h"
#include "vma_allocator.h"
#include "vulkan_device.h"
#include "vulkan_instance.h"
#include "window.h"
#include <chrono>
#include <cstdlib>
#include <stop_token>
#ifdef ENGINE_DEBUG
#include "core_export_struct.h"
#include "hot_reload.h"
#endif
#include <print>
#include <thread>

int main(int argc, char *argv[]) {

  std::print("argc: {}\n", argc);

  for (int i = 0; i < argc; i++) {
    std::print("argv[{}]: {}\n", i, argv[i]);
  }

#ifdef ENGINE_DEBUG

  // Get the directory containing the executable
  std::string exe_path = argv[0];
  std::string exe_dir = exe_path.substr(0, exe_path.find_last_of("/\\"));
  std::string core_path = exe_dir + "/lib/libcored.so";

  // Initialize hot reload system for core library
  HotReload core("core", core_path);

  // Create and store the coreState in the HotReload object's data
  core.setData(new coreState{nullptr, nullptr, nullptr});

  // Register load callback
  core.registerLoadCallback("core_load", [&core](void *data) {
    std::print("[Main] Core library loaded successfully\n");

    coreState *state = static_cast<coreState *>(data);
    auto lib_on_load_func = (void (*)(void *))core.getSymbol("lib_on_load");
    if (!lib_on_load_func) {
      std::print(stderr,
                 "[HotReload] Could not find symbol 'lib_on_load': {}\n",
                 dlerror());
    }

    if (lib_on_load_func) {
      lib_on_load_func(state);
    }
  });

  // Register unload
  core.registerUnloadCallback("core_unload", [&core](void *data) {
    std::print("[Main] Core library unloading\n");

    coreState *state = static_cast<coreState *>(data);
    auto lib_on_unload_func = (void (*)(void *))core.getSymbol("lib_on_unload");

    if (lib_on_unload_func) {
      lib_on_unload_func(state);
    }
  });

  // Register a destroy callback to be executed only when core is destroyed
  core.registerDestroyCallback("core_cleanup", [&core](void *data) {
    std::print("[Main] Core library cleanup\n");

    // Call the library's destroy function which handles singleton cleanup
    // This ensures global pointers are cleared after deletion
    void *symbol = core.getSymbol("lib_on_destroy");
    if (symbol) {
      auto lib_on_destroy_func = reinterpret_cast<void (*)(void *)>(symbol);
      lib_on_destroy_func(data);
    } else {
      std::print(stderr,
                 "[Main] Warning: lib_on_destroy not found, manual cleanup\n");
      // Fallback: manual cleanup if symbol not found (shouldn't happen)
      coreState *state = static_cast<coreState *>(data);
      if (state) {
        if (state->thread) {
          state->thread->shutdown();
          delete state->thread;
        }
        if (state->memory) {
          state->memory->shutdown();
          delete state->memory;
        }
        if (state->config) {
          state->config->shutdown();
          delete state->config;
        }
        delete state;
        core.setData(nullptr);
      }
    }
    // Clear the HotReload data pointer
    core.setData(nullptr);
  });

  // Register reload callback
  core.registerReloadCallback("core_reload", [&core](void *data) {
    std::print("[Main] Core library reloading\n");

    coreState *state = static_cast<coreState *>(data);
    auto lib_on_reload_func = (void (*)(void *))core.getSymbol("lib_on_reload");

    if (lib_on_reload_func) {
      lib_on_reload_func(state);
    }
  });

  if (!core.load()) {
    std::print(stderr, "Failed to load core library!\n");
    // Cleanup on failure
    coreState *state = static_cast<coreState *>(core.getData());
    if (state) {
      if (state->thread) {
        delete state->thread;
      }
      if (state->memory) {
        delete state->memory;
      }
      if (state->config) {
        delete state->config;
      }
      delete state;
      core.setData(nullptr);
    }
    return EXIT_FAILURE;
  }

  std::print("\n=== Starting Hot Reload Loop ===\n");

  auto funcReloadCheck = [&core]() {
    // Check for library changes and reload if needed
    if (core.checkAndReloadIfNeeded()) {
      std::print(">>> Core library reloaded! <<<\n\n");
    }
  };

  std::jthread hotReload;
  hotReload = std::jthread([&funcReloadCheck](std::stop_token stoken) {
    while (!stoken.stop_requested()) {
      funcReloadCheck();
      std::this_thread::sleep_for(std::chrono::seconds(5));
    }
  });
#endif // ENGINE_DEBUG

  // Configure multi-GPU if available
  core::GPUConfig gpuConfig;
  gpuConfig.enableMultiGPU = false;
  gpuConfig.gpuCount = 0; // Auto-detect
  core::Config::instance().setGPUConfig(gpuConfig);

  device::VulkanInstance inst;
  inst.initialize();

  device::DeviceManager dMan;
  device::VulkanDeviceConfig devConfig;
  devConfig.surface = nullptr;
  devConfig.enableMultiGPU =
      core::Config::instance().getGPUConfig().enableMultiGPU;
  devConfig.preferredGPUIndex =
      core::Config::instance().getGPUConfig().preferredGPUIndex;
  dMan.initialize(inst, devConfig);

  // Update GPU count in config based on actual device count
  gpuConfig.gpuCount = static_cast<uint32_t>(dMan.getDeviceCount());
  core::Config::instance().setGPUConfig(gpuConfig);

  device::VMAManager vMan;
  vMan.initialize(inst.getRaiiInstance(), dMan);

  window::WindowManager wMan;
  wMan.initialize();

  // Build secondary GPU list for windows
  std::vector<device::GPUDevice *> secondaryGPUs;
  for (size_t i = 1; i < dMan.getDeviceCount(); ++i) {
    secondaryGPUs.push_back(dMan.getDevice(static_cast<uint32_t>(i)));
  }

  // Configure swapchain
  window::SwapchainConfig swapConf;
  swapConf.vsync = core::Config::instance().getLoopConfig().enableVSync;

  // Window config with rendering chain initialization
  window::WindowConfig wConf;
  wConf.mainGPU = &dMan.getPrimaryDevice();
  wConf.secondaryGPUs = secondaryGPUs;
  wConf.vulkanInstance = &inst.getRaiiInstance();
  wConf.allocator = &vMan.getPrimaryAllocator();
  wConf.swapchainConfig = swapConf;

  // Create window 1 (automatically initializes renderer and swapchain)
  wConf.title = "SapphireEngine - Window 1";
  window::Window *win1 = wMan.createWindow(wConf);

  // Create Window 2 (automatically initializes renderer and swapchain)
  wConf.title = "SapphireEngine - Window 2";
  window::Window *win2 = wMan.createWindow(wConf);

  // Update loop config to match window count
  {
    core::LoopConfig loopCfg = core::Config::instance().getLoopConfig();
    loopCfg.mainLoopCount = static_cast<uint32_t>(wMan.getWindowCount());
    core::Config::instance().setLoopConfig(loopCfg);
  }

  // Initialize thread pools based on effective thread allocation
  {
    auto &tm = core::ThreadManager::instance();
    auto effectiveAlloc =
        core::Config::instance().getEffectiveThreadAllocation();

    // Create worker pool if not already created
    if (!tm.hasPool("worker")) {
      core::ThreadPoolConfig workerCfg;
      workerCfg.name = "worker";
      workerCfg.type = core::PoolType::Worker;
      workerCfg.threadCount = effectiveAlloc.workerThreads;
      tm.createPool(workerCfg);
      std::print("[Main] Created worker pool with {} threads\n",
                 effectiveAlloc.workerThreads);
    }

    // Create loop pool for window event polling / rendering loops
    if (!tm.hasPool("loop")) {
      core::ThreadPoolConfig loopCfg;
      loopCfg.name = "loop";
      loopCfg.type = core::PoolType::Loop;
      loopCfg.threadCount = effectiveAlloc.loopThreads;
      tm.createPool(loopCfg);
      std::print("[Main] Created loop pool with {} threads\n",
                 effectiveAlloc.loopThreads);
    }

    // Create GPU pool(s) for GPU operations
    if (!tm.hasPool("gpu")) {
      core::ThreadPoolConfig gpuCfg;
      gpuCfg.name = "gpu";
      gpuCfg.type = core::PoolType::GPU;
      gpuCfg.threadCount = effectiveAlloc.gpuThreads;
      tm.createPool(gpuCfg);
      std::print("[Main] Created GPU pool with {} threads\n",
                 effectiveAlloc.gpuThreads);
    }
  }

  // Register close callbacks on both windows
  if (win1) {
    win1->setEventCallback(
        [](const window::WindowEvent &) {
          std::print("[Main] Window 1 close requested\n");
        },
        window::WindowEventType::Close);

    win1->setEventCallback(
        [](const window::WindowEvent &event) {
          std::print("[Main] Window 1 resized to {}x{}\n", event.width,
                     event.height);
        },
        window::WindowEventType::Resize);
  }

  if (win2) {
    win2->setEventCallback(
        [](const window::WindowEvent &) {
          std::print("[Main] Window 2 close requested\n");
        },
        window::WindowEventType::Close);

    win2->setEventCallback(
        [](const window::WindowEvent &event) {
          std::print("[Main] Window 2 resized to {}x{}\n", event.width,
                     event.height);
        },
        window::WindowEventType::Resize);
  }

  // Initialize shader manager and compile triangle shaders
  device::ShaderManager sMan;
  sMan.initialize(dMan.getPrimaryDevice());

  // Compile the triangle shader (vertex, geometry, fragment)
  device::CompiledShader *vertShader =
      sMan.loadShader("triangle.slang", "vertMain", device::ShaderStage::Vertex);
  device::CompiledShader *geomShader =
      sMan.loadShader("triangle.slang", "geomMain",
                      device::ShaderStage::Geometry);
  device::CompiledShader *fragShader =
      sMan.loadShader("triangle.slang", "fragMain",
                      device::ShaderStage::Fragment);

  if (vertShader && geomShader && fragShader) {
    std::print("[Main] Triangle shaders compiled successfully\n");
  } else {
    std::print(stderr, "[Main] Failed to compile triangle shaders\n");
  }

  // Create graphics pipelines for each window
  std::unordered_map<uint32_t, vk::raii::Pipeline> pipelines;
  std::unordered_map<uint32_t, vk::raii::PipelineLayout> pipelineLayouts;

  if (vertShader && geomShader && fragShader) {
    for (const auto &win : wMan.getWindows()) {
      window::Renderer *renderer = win->getRenderer();
      if (!renderer) {
        continue;
      }

      // Shader stages
      std::array<vk::PipelineShaderStageCreateInfo, 3> shaderStages = {
          vertShader->getStageInfo(), geomShader->getStageInfo(),
          fragShader->getStageInfo()};

      // Vertex input: no vertex buffers (hardcoded in shader)
      vk::PipelineVertexInputStateCreateInfo vertexInput{};

      // Input assembly: triangle list
      vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
          {}, vk::PrimitiveTopology::eTriangleList, vk::False};

      // Viewport and scissor (dynamic)
      vk::PipelineViewportStateCreateInfo viewportState{{}, 1, nullptr, 1,
                                                        nullptr};

      // Rasterizer
      vk::PipelineRasterizationStateCreateInfo rasterizer{
          {},
          vk::False,
          vk::False,
          vk::PolygonMode::eFill,
          vk::CullModeFlagBits::eNone,
          vk::FrontFace::eClockwise,
          vk::False,
          0.0f,
          0.0f,
          0.0f,
          1.0f};

      // Multisampling
      vk::PipelineMultisampleStateCreateInfo multisampling{
          {}, vk::SampleCountFlagBits::e1, vk::False};

      // Depth stencil
      vk::PipelineDepthStencilStateCreateInfo depthStencil{
          {}, vk::True, vk::True, vk::CompareOp::eLess, vk::False, vk::False};

      // Color blending
      vk::PipelineColorBlendAttachmentState colorBlendAttachment{
          vk::False,
          vk::BlendFactor::eOne,
          vk::BlendFactor::eZero,
          vk::BlendOp::eAdd,
          vk::BlendFactor::eOne,
          vk::BlendFactor::eZero,
          vk::BlendOp::eAdd,
          vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
              vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

      vk::PipelineColorBlendStateCreateInfo colorBlending{
          {}, vk::False, vk::LogicOp::eCopy, colorBlendAttachment};

      // Dynamic states
      std::array<vk::DynamicState, 2> dynamicStates = {
          vk::DynamicState::eViewport, vk::DynamicState::eScissor};
      vk::PipelineDynamicStateCreateInfo dynamicState{{}, dynamicStates};

      // Pipeline layout (empty - no descriptors needed)
      vk::PipelineLayoutCreateInfo layoutInfo{};

      try {
        auto layout = vk::raii::PipelineLayout(
            dMan.getPrimaryDevice().getRaiiDevice(), layoutInfo);

        vk::GraphicsPipelineCreateInfo pipelineInfo{
            {},           shaderStages,    &vertexInput, &inputAssembly,
            nullptr,      &viewportState,  &rasterizer,  &multisampling,
            &depthStencil, &colorBlending, &dynamicState, *layout,
            renderer->getRenderPass()};

        auto pipeline = vk::raii::Pipeline(
            dMan.getPrimaryDevice().getRaiiDevice(), nullptr, pipelineInfo);

        std::print("[Main] Pipeline created for window: {}\n",
                   win->getTitle());

        pipelineLayouts.emplace(win->getWindowId(), std::move(layout));
        pipelines.emplace(win->getWindowId(), std::move(pipeline));
      } catch (const vk::SystemError &e) {
        std::print(stderr, "[Main] Failed to create pipeline for {}: {}\n",
                   win->getTitle(), e.what());
      }
    }
  }

  int frame = 0;
  while (!wMan.checkWindowsVectorEmpty()) {
    std::print("Frame {}\n", frame);
    std::print("\n");

    wMan.pollAllEvents();

    // Remove windows that should close
    std::vector<window::Window *> toRemove;
    for (const auto &win : wMan.getWindows()) {
      if (win->shouldClose()) {
        toRemove.push_back(win.get());
      }
    }
    for (auto *win : toRemove) {
      std::print("[Main] Destroying closed window: {}\n", win->getTitle());
      // Remove pipeline resources before destroying window
      pipelines.erase(win->getWindowId());
      pipelineLayouts.erase(win->getWindowId());
      wMan.destroyWindow(win);
    }

    if (wMan.checkWindowsVectorEmpty()) {
      break;
    }

    // Render a triangle in each window
    for (const auto &win : wMan.getWindows()) {
      window::Renderer *renderer = win->getRenderer();
      if (!renderer || !renderer->isInitialized()) {
        continue;
      }

      auto pipeIt = pipelines.find(win->getWindowId());
      if (pipeIt == pipelines.end()) {
        continue;
      }

      auto *syncObjects = renderer->beginFrame();
      if (!syncObjects) {
        continue;
      }

      vk::CommandBuffer cmd = syncObjects->commandBuffer;
      window::Swapchain *swapchain = renderer->getSwapchain();
      vk::Extent2D extent = swapchain->getExtent();

      // Begin render pass
      auto clearValues = renderer->getClearValues();
      uint32_t imageIndex = renderer->getCurrentImageIndex();
      vk::RenderPassBeginInfo renderPassInfo{
          renderer->getRenderPass(),
          **swapchain->getFrame(imageIndex).framebuffer,
          vk::Rect2D{{0, 0}, extent}, clearValues};

      cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

      // Set dynamic viewport and scissor
      vk::Viewport viewport{0.0f,
                             0.0f,
                             static_cast<float>(extent.width),
                             static_cast<float>(extent.height),
                             0.0f,
                             1.0f};
      cmd.setViewport(0, viewport);

      vk::Rect2D scissor{{0, 0}, extent};
      cmd.setScissor(0, scissor);

      // Bind pipeline and draw triangle
      cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeIt->second);
      cmd.draw(3, 1, 0, 0); // 3 vertices, 1 instance

      cmd.endRenderPass();

      renderer->endFrame(*syncObjects, imageIndex);
    }

    frame++;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  // Wait for all GPU work to finish before cleanup
  dMan.getPrimaryDevice().waitIdle();

#ifdef ENGINE_DEBUG
  hotReload.request_stop();
  hotReload.join();
#endif

  return EXIT_SUCCESS;
}
