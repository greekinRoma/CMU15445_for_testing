
//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// arc_replacer.h
//
// Identification: src/include/buffer/arc_replacer.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <list>
#include <memory>
#include <mutex>  // NOLINT
#include <optional>
#include <unordered_map>

#include "common/config.h"
#include "common/macros.h"

namespace bustub {

enum class AccessType { Unknown = 0, Lookup, Scan, Index };

enum class ArcStatus { MRU, MFU, MRU_GHOST, MFU_GHOST };
// enum class 强类型枚举，表示节点的状态
// MRU表示节点在MRU中，MFU表示节点在MFU中，MRU_GHOST表示节点在MRU的ghost中，MFU_GHOST表示节点在MFU的ghost中
//MRU 最近只访问一次的页；新页未命中时进入这里。链表按访问时间排序。
//MFU 最近被访问至少两次的页（热点页）；mru_ 中页面被访问后就升级刀这里
//MRU_GHOST 曾经在MRU，后来被淘汰的页，只保留page_id，不保留页面数据
//MFU_GHOST 曾经在MFU，后来被被淘汰的页，同样只保留paege_id，不保留页面数据
/*
未命中任何链表 -> MRU（插入mru_队首）
MRU中的页再次被访问 -> MFU（移到mfu_队首）
MFU中的页再次被访问 -> MFU（移到mfu_队首）
命中 MRU_GHOST -> MRU（移到mru_队首）
命中 MFU_GHOST -> MFU（移到mfu_队首）

Evict() 从 mru_ 淘汰 -> MRU_GHOST
Evict() 从 mfu_ 淘汰 -> MFU_GHOST
从mfu_淘汰的页直接进入mfu_ghost_,之后若再次被访问，也是回到mfu_，不经过mru_，因为mfu_表示热点页，热点页不可能是新页
*/

// TODO(student): You can modify or remove this struct as you like.
struct FrameStatus {
  page_id_t page_id_;
  /*
  page_id_ 表示当前页面ID
  */
  frame_id_t frame_id_;
  // 页面所在的物理内存槽位 ID
  // frame_id_t 是 页面的身份证，frame_id 是它当前占用的内存房间号，一个是逻辑身份，一个是物理位置
  /*
  Page_id 是数据库页的逻辑编号
  frame_id 是内存数组的槽位下标
  Frame 是容器，Page 是被缓存的数据；Frame中保存的是Page的副本/内容
  Page 是数据库数据的逻辑单位，通常持久化在磁盘/SSD 文件中；Frame 是内存 Buffer Pool 中的物理槽位，用来承载某个 Page 在内存中的副本。
  */
  bool evictable_;
  // 该Frame是否允许被淘汰
  // evictable_是替换器的开关，决定frame是否可以被替换器淘汰
  ArcStatus arc_status_;
  // 该页面属于ARC的哪个链表
  /*
  当前的状态，表示该页面在ARC算法中的位置。ArcStatus是一个枚举类型，定义了四种可能的状态：
  */
  FrameStatus(page_id_t pid, frame_id_t fid, bool ev, ArcStatus st)
      : page_id_(pid), frame_id_(fid), evictable_(ev), arc_status_(st) {}
};

/**
 * ArcReplacer implements the ARC replacement policy.
 */
class ArcReplacer {
 public:
  explicit ArcReplacer(size_t num_frames);

  DISALLOW_COPY_AND_MOVE(ArcReplacer);

  /**
   * TODO(P1): Add implementation
   *
   * @brief Destroys the LRUReplacer.
   */
  ~ArcReplacer() = default;

  auto Evict() -> std::optional<frame_id_t>;
  // 选择并淘汰一个一个可淘汰的frame，返回frame_id，如果没有可淘汰的frame，返回std::nullopt
  void RecordAccess(frame_id_t frame_id, page_id_t page_id, AccessType access_type = AccessType::Unknown);
  // 记录一次页面访问
  void SetEvictable(frame_id_t frame_id, bool set_evictable);
  // 设置某个Frame能否被淘汰
  void Remove(frame_id_t frame_id);
  // 主动将某个Frame从替换器中移除
  auto Size() -> size_t;
  // 返回当前可淘汰的Frame数量

 private:
  // TODO(student): implement me! You can replace or remove these member variables as you like.
  std::list<frame_id_t> mru_;
  // 最近访问过的页面，表示曾经访问
  std::list<frame_id_t> mfu_;
  // 被重复访问的页面，表示最近访问
  std::list<page_id_t> mru_ghost_;
  // 曾经再MRU中，后来被淘汰的页面
  std::list<page_id_t> mfu_ghost_;
  // 曾经在MFU中，后来被淘汰的页面

  /* record entries in mru_ and mfu_
   * this uses frame_id_t to guarantee no duplicate records for the same
   * frame when they are alive */
  std::unordered_map<frame_id_t, std::shared_ptr<FrameStatus>> alive_map_;
  // 用于记录alive_map的状态
  // <frame_id, FrameStatus>，FrameStatus中包含page_id，evictable，arc_status
  std::unordered_map<page_id_t, std::shared_ptr<FrameStatus>> ghost_map_;
  // 用于记录ghost_map的状态

  /* alive, evictable entries count */
  size_t curr_size_{0};
  // 可以被驱逐的真实缓存页
  // 其中{0}表示 curr_size_ 的初始化为0
  /* p as in original paper */
  size_t mru_target_size_{0};
  // ARC算法中的参数p，控制MRU和MFU的容量分配倾向
  /* c as in original paper */
  size_t replacer_size_;
  // ARC 算法中的参数c，即缓存容量
  std::mutex latch_;
  //目前所使用的锁

  // TODO(student): You can add member variables / functions as you like.
};

}  // namespace bustub
