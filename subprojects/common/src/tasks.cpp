#include "tasks.h"
#include "BS_thread_pool.hpp"
#include <cstdint>
#include <print>
#include <stdexcept>
#include <thread>

Tasks &Tasks::get_instance() {
  static Tasks instance;
  return instance;
}

Tasks::Tasks()
    : num_threads(std::thread::hardware_concurrency() * 0.75), num_gpus(0),
      pool(num_threads) {}

Tasks::~Tasks() {
  pool.wait();
  std::print("Tasks destructor executed\n");
};

void Tasks::set_threads(uint16_t amount) {
  num_threads = amount;
  if (num_threads == 0 || num_threads > std::thread::hardware_concurrency()) {
    throw std::runtime_error(
        "Thread pool can't be 0 or more than " +
        std::to_string(std::thread::hardware_concurrency()) + "\n");
  }
  pool.reset(num_threads - num_gpus);
}

void Tasks::add_gpu() {
  if (num_gpus >= num_threads - 1) {
    throw std::runtime_error("Theres too many threads allocatated to gpus");
  }
  num_gpus++;
  set_threads(num_threads);
}

void Tasks::remove_gpu() {
  if (num_gpus == 0 || num_threads > std::thread::hardware_concurrency()) {
    throw std::runtime_error("Theres no thread dedicated to a gpu to remove");
  }
  num_gpus--;
  set_threads(num_threads);
}
