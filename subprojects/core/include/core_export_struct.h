#ifndef CORE_EXPORT_STRUCT_H_
#define CORE_EXPORT_STRUCT_H_
#ifdef ENGINE_DEBUG

#include "engine_config.h"
#include "memory_manager.h"
#include "thread_manager.h"

struct coreState {
  core::ThreadManager *thread;
  core::MemoryManager *memory;
  core::EngineConfig *config;
};

#endif
#endif // CORE_EXPORT_STRUCT_H_
