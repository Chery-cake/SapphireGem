#pragma once

#include <BS_thread_pool.hpp>
#include <cstdint>
#include <future>
#include <type_traits>
#include <utility>

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
  auto add_task(F &&func, const BS::priority_t priority = 0)
      -> std::future<std::invoke_result_t<std::decay_t<F>>> {
    return pool.submit_task(std::forward<F>(func), priority);
  }
};
