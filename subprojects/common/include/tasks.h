#pragma once

#include <cstdint>
#include <future>
#include <type_traits>
#include <utility>

#ifdef __INTELLISENSE__
#include <BS_thread_pool.hpp>
#else
import BS.thread_pool;
#endif

class Tasks {
private:
#define ThreadPool BS::thread_pool<BS::tp::pause>

  uint16_t num_threads;
  uint16_t num_gpus;
  ThreadPool pool;

  Tasks();

public:
  ~Tasks();
  Tasks(Tasks &&) = delete;
  Tasks &operator=(Tasks &&) = delete;
  Tasks(const Tasks &) = delete;
  Tasks &operator=(const Tasks &) = delete;
  static Tasks &get_instance();

  void set_threads(uint16_t amount);
  void add_gpu();
  void remove_gpu();

  template <typename F>
  std::future<std::invoke_result_t<std::decay_t<F>>>
  add_task(F &&func, const BS::priority_t priority = 0) {
    return pool.submit_task(std::forward<F>(func), priority);
  }
};
