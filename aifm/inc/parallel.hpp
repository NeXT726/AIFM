#pragma once

#include "thread.h"

#include "cb.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace far_memory {

template <typename Task> class Parallelizer {
private:
  std::atomic<bool> master_done_{false};
  std::atomic<bool> master_up_{false};
  std::unique_ptr<std::unique_ptr<CircularBuffer<Task, true>>[]> task_queues_;
  std::vector<rt::Thread> threads_;
  uint32_t enqueue_thread_id_ = 0;
  uint32_t num_slaves_;

public:
  NOT_COPYABLE(Parallelizer);
  NOT_MOVEABLE(Parallelizer);

  // @num_slaves：多少个线程 - 就有多少个队列 task_queue
  // @task_queues_depth：每个队列的深度
  Parallelizer(uint32_t num_slaves, uint32_t task_queues_depth);
  virtual void master_fn() = 0;
  virtual void slave_fn(uint32_t tid) = 0;
  // 将任务task加入任一队列，依次遍历所有队列找到一个加入成功的就可以返回了
  template <typename T> void master_enqueue_task(T &&task);
  bool slave_dequeue_task(uint32_t tid, Task *task);
  bool slave_can_exit(uint32_t tid);
  void spawn(Status *slaves_status);
  void execute();
};

} // namespace far_memory

#include "internal/parallel.ipp"
