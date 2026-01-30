#ifndef CORE_EXPORT_STRUCT_H_
#define CORE_EXPORT_STRUCT_H_
#ifdef ENGINE_DEBUG

#include "memory_manager.h"
#include "tag_registry.h"
#include "thread_manager.h"

struct coreState {
  core::ThreadManager *thread;
  core::MemoryManager *memory;
  core::TagRegistry *tags;
};

#endif
#endif // CORE_EXPORT_STRUCT_H_
