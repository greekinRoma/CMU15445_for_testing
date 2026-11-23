//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// disk_scheduler.h
//
// Identification: src/include/storage/disk/disk_scheduler.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <future>  // NOLINT
#include <optional>
#include <thread>  // NOLINT
#include <vector>

#include "common/channel.h"
#include "storage/disk/disk_manager.h"

namespace bustub {

/**
 * @brief Represents a Write or Read request for the DiskManager to execute.
 */
struct DiskRequest {
  /** Flag indicating whether the request is a write or a read. */
  bool is_write_;
  // true = 写， false = 读
  /**
   *  Pointer to the start of the memory location where a page is either:
   *   1. being read into from disk (on a read).
   *   2. being written out to disk (on a write).
   */
  char *data_;
  // 读：数据要读进哪块内存；
  // 写：数据从哪块内存写出。
  /** ID of the page being read from / written to disk. */
  page_id_t page_id_;
  // page id 是页的唯一标识符，page_id_t 是 int32_t 类型
  /** Callback used to signal to the request issuer when the request has been completed. */
  std::promise<bool> callback_;
  /*
  一次磁盘I/O 的完成统治，它把worker线程和发起请求的线程连接起来。std::promise只是写端，配套的“读端”是调用方手的std::future<bool>。
  这里不是函数指针意义上的callback。传统回调是“发起者注册一个函数，完成时由别人调用它”；这里注册的std::promise<bool>这个对象：

  worker 线程调用 callback_.set_value(ture)，相当于“触发回调”
  发起者手里拿着与它配对的 std::future<bool>，调用future.get()，相当于“回调被唤醒/读取结果”
  
  它是每个 DiskRequest 自带的完成回执端：后台线程在磁盘 I/O 真正完成后调用 set_value()，唤醒持有对应 std：future 的请求发起者，并告知这次I/O是否成功

  std::future 是 std::promise 那对共享状态的读端/取货凭证：调用方用它来等待异步操作完成，并在结果就绪后取结果。

  example: 
  auto promise = dist_scheduler->CreatePromise();
  suto future = promise.get_future();
  DiskRequest req{
    .is_write_ = true,
    .data_ = data,
    .page_id_ = page_id,
    .callback_ = std::move(promise)
  };
  disk_schduler->Schedule(requests);
  // 等待异步操作完成
  bool success = future.get();
  // 这个相当于用于异步结构
  1. 创建一个 promise<bool>，作为结果的“写端”，将来用来存放一个 bool 结果或异常。
  std::promise<bool> p;
  // 创建一个 promise<bool>, 作为结果的“写端”，将来用来存放一个 bool 结果或异常。
  std::thread t([&p] {
    // 执行异步操作
    bool result = perform_async_operation();
    // 设置结果
    p.set_value(result);
  });
  // [&p] { ... } 是一个 lambda 表达式，
  std::thread t( ... );  创建一个新线程，名字叫t，让这个新线程区执行括号里面的任务
  [&p] {
    bool result = perform_async_operation();
    p.set_value(result);
}
  这整块是一个匿名函数，可以把它理解成任务清单
  bool result = perform_async_operation();  执行异步操作，返回一个 bool 结果
  p.set_value(result);  将结果写入 promise 对象中，通知等待的
  std::thread t([&p] { ... }); 开一个新线程，让新线程照着 { ... } 这张任务清单去干。
  [p] 是捕获列表，表示 lambda 表达式可以访问外部变量 p，这里是引用捕获（&p），所以 lambda 可以修改 p。
  */
};

/**
 * @brief The DiskScheduler schedules disk read and write operations.
 *
 * A request is scheduled by calling DiskScheduler::Schedule() with an appropriate DiskRequest object. The scheduler
 * maintains a background worker thread that processes the scheduled requests using the disk manager. The background
 * thread is created in the DiskScheduler constructor and joined in its destructor.
 */
// DiskScheduler 负责调度磁盘读写操作。它维护一个后台工作线程，处理调度的请求，并使用 DiskManager 执行实际的磁盘 I/O 操作。请求通过调用 DiskScheduler::Schedule() 方法来调度，传入一个适当的 DiskRequest 对象。后台线程在 DiskScheduler 构造函数中创建，并在析构函数中进行 join 操作，以确保线程正确终止。
class DiskScheduler {
 public:
  explicit DiskScheduler(DiskManager *disk_manager);
  ~DiskScheduler();

  void Schedule(std::vector<DiskRequest> &requests);

  void StartWorkerThread();

  using DiskSchedulerPromise = std::promise<bool>;

  /**
   * @brief Create a Promise object. If you want to implement your own version of promise, you can change this function
   * so that our test cases can use your promise implementation.
   *
   * @return std::promise<bool>
   */
  auto CreatePromise() -> DiskSchedulerPromise { return {}; };

  /**
   * @brief Deallocates a page on disk.
   *
   * Note: You should look at the documentation for `DeletePage` in `BufferPoolManager` before using this method.
   *
   * @param page_id The page ID of the page to deallocate from disk.
   */
  void DeallocatePage(page_id_t page_id) { disk_manager_->DeletePage(page_id); }

 private:
  /** Pointer to the disk manager. */
  DiskManager *disk_manager_ __attribute__((__unused__));
  /** A shared queue to concurrently schedule and process requests. When the DiskScheduler's destructor is called,
   * `std::nullopt` is put into the queue to signal to the background thread to stop execution. */
  Channel<std::optional<DiskRequest>> request_queue_;
  /** The background thread responsible for issuing scheduled requests to the disk manager. */
  std::optional<std::thread> background_thread_;
};
}  // namespace bustub
