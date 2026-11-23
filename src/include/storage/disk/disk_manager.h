//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// disk_manager.h
//
// Identification: src/include/storage/disk/disk_manager.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>  // NOLINT
#include <mutex>   // NOLINT
#include <string>
#include <unordered_map>
#include <vector>

#include "common/config.h"
#include "common/logger.h"

namespace bustub {

/**
 * DiskManager takes care of the allocation and deallocation of pages within a database. It performs the reading and
 * writing of pages to and from disk, providing a logical file layer within the context of a database management system.
 *
 * DiskManager uses lazy allocation, meaning that it only allocates space on disk when it is first accessed. It
 * maintains a mapping of page ids to their corresponding offsets in the database file. When a page is deleted, it is
 * marked as free and can be reused by future allocations.
 */
// DiskManager 负责数据库中页的分配和释放。它执行页的读写操作，为数据库管理系统提供逻辑文件层。DiskManager 使用惰性分配，这意味着它只在首次访问时才在磁盘上分配空间。它维护一个页 ID 到数据库文件中相应偏移量的映射。当一个页被删除时，它会被标记为可用，并可以被未来的分配重用。
class DiskManager {
 public:
  explicit DiskManager(const std::filesystem::path &db_file);
// std::filesystem::path C++ 17 引入文件系统路径，用来表示文件路径，支持跨平台。db_file 是数据库文件的路径。
  /** FOR TEST / LEADERBOARD ONLY, used by DiskManagerMemory */
  DiskManager() = default;

  virtual ~DiskManager() = default;
  /*
  virtual 的作用是动态绑定：当通过基类指针或引用调用一个虚函数时，程序再运行时看对象的实际类型，决定调用哪个版本。 
  virtual ~DiskManager() = default;                              // 虚析构
  virtual void WritePage(page_id_t page_id, const char *page_data);
  virtual void ReadPage(page_id_t page_id, char *page_data);
  virtual void DeletePage(page_id_t page_id);
  */

  void ShutDown();
  // shutDown 关闭的是DiskManager的两个文件流，不是“缓存池里的page”，也不会遍历，写回BPM脏页
  // BPM = BufferPoolManager,
  /**
   * Write a page to the database file.
   * @param page_id id of the page
   * @param page_data raw page data
   */
  virtual void WritePage(page_id_t page_id, const char *page_data);
  // page_id 是数据库页的逻辑编号，标识“这是哪一页数据”。
  // frame_id 是缓存池内存数据的槽位下标，（0-num_frames-1）
  /**
   * Read a page from the database file.
   * @param page_id id of the page
   * @param[out] page_data output buffer
   */
  virtual void ReadPage(page_id_t page_id, char *page_data);

  /**
   * Delete a page from the database file. Reclaim the disk space.
   * @param page_id id of the page
   */
  virtual void DeletePage(page_id_t page_id);
  // DiskManager::DeletePage 的作用，一句话：它不擦除页数据，也不缩小文件；它删除 page_id → 文件偏移 的映射，并把这个偏移标记为“可复用”。

  void WriteLog(char *log_data, int size);
  // 写log

  auto ReadLog(char *log_data, int size, int offset) -> bool;
  // 读log

  auto GetNumFlushes() const -> int;
  // 不写前置返回类型 函数名 不改对象 尾置返回类型：返回 int
  // num_flushes_ 是 DiskManager 成员变量，记录写 log 的次数。GetNumFlushes() 返回这个值。
  // 这个函数是 const 成员函数，表示它不会修改对象的状态。

  auto GetFlushState() const -> bool;
  // true：还有日志内容没刷到磁盘
  // false：当前没有待刷的日志

  auto GetNumWrites() const -> int;
  // 返回DiskManager 通过 WritePage 实际写出的页数
  /*
  只有 WritePage 成功写出一个页时，num_writes_ 才会加1
  */
  auto GetNumDeletes() const -> int;
  // 返回DiskManager 成功执行的“页删除次数”，也就是成员 num_deletes_ 的值。也就是成员 num_deletes_ 的值
  /**
   * Sets the future which is used to check for non-blocking flushes.
   * @param f the non-blocking flush check
   */
  inline void SetFlushLogFuture(std::future<void> *f) { flush_log_f_ = f; }
  // 它把调用方提供的，代表一次异步日志 flush 完成型号的 std::future 指针登记到DiskManager，供WriteLog在写下一批日志前等待上一次flush完成。DiskManager 只负责写日志到文件流，不负责 flush 到磁盘。flush_log_f_ 是一个 std::future<void> 指针，指向一个异步操作的结果。调用 SetFlushLogFuture() 可以设置这个指针，供 DiskManager 在写下一批日志前等待上一次 flush 完成。
  /** Checks if the non-blocking flush future was set. */
  inline auto HasFlushLogFuture() -> bool { return flush_log_f_ != nullptr; }
  // HasFlushLogFuture() 是一个查询接口，只回答一个问题：DiskManager 当前有没有登记过一个“非阻塞日志 flush”的 future？
  /** @brief returns the log file name */
  inline auto GetLogFileName() const -> std::filesystem::path { return log_file_name_; }
  // GetLogFileName() 返回日志文件的路径。log_file_name_ 是 DiskManager 的成员变量，记录日志文件的路径。
  /** @brief returns the size of disk space in use */
  auto GetDbFileSize() -> size_t {
    auto file_size = GetFileSize(db_file_name_);
    if (file_size < 0) {
      LOG_DEBUG("I/O error: Fail to get db file size");
      return -1;
    }
    return static_cast<size_t>(file_size);
  }
  //返回数据库文件当前子啊磁盘上占用的字节数
 protected:
  int num_flushes_{0};
  //日志 flush 次数
  int num_writes_{0};
  //页写次数
  int num_deletes_{0};
  // 页删除次数
  // 页 是 逻辑数据单位，它既不是专指缓存里的，也不是专指硬盘上的
  // 删除后就没有“撤销/恢复”接口了，这个页在数据库里就失效了。

  /** @brief The capacity of the file used for storage on disk. */
  size_t page_capacity_{DEFAULT_DB_IO_SIZE};
  // page_capacity_ 是DiskManager的“数据库文件容量”，单位是“页”：它表示这个数据库文件当前最多能容纳多少个页槽位，而不是已经用了多少页。

 private:
  auto GetFileSize(const std::string &file_name) -> int;
  // 对外返回数据库的字节大小

  auto AllocatePage() -> size_t;
  // DiskManager 是一个私有辅助函数；它为某个页在数据库文件里分配一个槽位，返回改槽位文件中的字节偏移
  // AllocatePage() 的作用是：在数据库文件里为一个页分配一个槽位，返回这个槽位在文件中的字节偏移。它会先检查 free_slots_ 是否有空闲槽位，如果有就复用；如果没有就扩展文件容量。

  // stream to write log file
  std::fstream log_io_;
  // 用来读写日志文件的流
  std::filesystem::path log_file_name_;
  // 日志文件的路径
  // stream to write db file
  std::fstream db_io_;
  // 用来读写数据库文件的流
  std::filesystem::path db_file_name_;
  // 数据库文件的路径

  // Records the offset of each page in the db file.
  std::unordered_map<page_id_t, size_t> pages_;
  // Records the free slots in the db file if pages are deleted, indicated by offset.
  std::vector<size_t> free_slots_;
  // DiskManage 的空闲文件槽位列表。它记录了数据库文件中哪些槽位是空闲的，可以被 AllocatePage() 复用。每个槽位对应一个页的字节偏移。

  bool flush_log_{false};
  // 是否有未刷完的日志内容
  // 调用方在flush 期间不干别的事情
  std::future<void> *flush_log_f_{nullptr};
  // With multiple buffer pool instances, need to protect file access
  // 异步 flush的完成凭证
  std::mutex db_io_latch_;
};

}  // namespace bustub
