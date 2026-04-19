#ifndef CORE_EXPORT_STRUCT_H_
#define CORE_EXPORT_STRUCT_H_
#ifdef ENGINE_DEBUG

#include "config.h"
#include "thread_manager.h"

struct coreState {
  core::ThreadManager *thread;
  core::Config *config;
};

#endif
#endif // CORE_EXPORT_STRUCT_H_
