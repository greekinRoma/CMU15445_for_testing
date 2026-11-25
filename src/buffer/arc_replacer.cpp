// :bustub-keep-private:
//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// arc_replacer.cpp
//
// Identification: src/buffer/arc_replacer.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/arc_replacer.h"
#include <optional>
#include "common/config.h"
#include "common/exception.h"
#include <algorithm>

// ============================================================================
//                         ARC Replacer 实现速查
// ----------------------------------------------------------------------------
// 【结构】
//   mru_        : 最近页(只访问过 1 次)   frame_id 链表,前端最老
//   mfu_        : 高频页(访问过 >= 2 次)  frame_id 链表,后端最老
//   mru_ghost_  : 从 mru_ 淘汰的页         page_id  链表,前端最老
//   mfu_ghost_  : 从 mfu_ 淘汰的页         page_id  链表,后端最老
//   alive_map_  : frame_id -> FrameStatus   (活页索引)
//   ghost_map_  : page_id  -> FrameStatus   (幽灵索引)
//   curr_size_  : 可驱逐活页数(只统计 evictable_ == true)
//   mru_target_size_ (p) : MRU 的目标大小,初始 0,范围 [0, c]
//
// 【方向约定】"离 ! 越近越新": [<-mru_ghost-][<-mru-]![-mfu->][->mfu_ghost->]
//   插入:    mru_.push_back         mfu_.push_front
//            mru_ghost_.push_back   mfu_ghost_.push_front
//   取最老:  mru_.front()           mfu_.back()
//            mru_ghost_.front()     mfu_ghost_.back()
//
// 【RecordAccess 四种情况】
//   1. 命中活页(alive_map_ 按 frame_id 查):
//        MRU -> 从 mru_ 摘出,插 mfu_ 前端,状态改 MFU
//        MFU -> 刷新到 mfu_ 前端
//        return
//   2. 命中幽灵(ghost_map_ 按 page_id 查):
//        删除前先取 b1 = |mru_ghost_|, b2 = |mfu_ghost_|
//        MRU_GHOST: delta = (b1 >= b2) ? 1 : b2/b1;  p = min(p + delta, c)
//        MFU_GHOST: delta = (b2 >= b1) ? 1 : b1/b2;  p = (p < delta) ? 0 : p - delta
//        删幽灵(链表 + ghost_map_),复活到 mfu_ 前端,状态 MFU
//        return
//   3. 全 miss:
//        4A: |mru_| + |mru_ghost_| == c 且 |mru_| < c
//            -> 删 mru_ghost_ 最老(前端) + ghost_map_
//        否则 4B: 四表合计 >= 2c
//            -> 删 mfu_ghost_ 最老(后端) + ghost_map_
//        新页: 状态 MRU, mru_.push_back(尾部)
//
// 【Evict 步骤】
//   curr_size_ == 0 -> return nullopt
//   选边: mru_.size() >= p ? 先 mru_ : 先 mfu_      (等号归 mru_)
//   mru_ 正扫(front 起)跳过 pinned;mfu_ 反扫(back 起)跳过 pinned
//   整侧都不行 -> 换另一侧;两侧都不行 -> return nullopt
//   取 pid -> alive_map_.erase -> 链表 remove
//   from mru_: mru_ghost_.push_back(pid) + 状态 MRU_GHOST
//   from mfu_: mfu_ghost_.push_front(pid) + 状态 MFU_GHOST
//   curr_size_--; return victim
//
// 【计数维护】curr_size_ 只由 SetEvictable(±1)、Evict(-1)、Remove(-1) 修改
//
// 【常见坑】
//   - 幽灵查表用 page_id,活页查表用 frame_id
//   - 先量尺寸/先读字段,再 erase/remove(迭代器会失效)
//   - size_t 减法防回绕: (p < delta) ? 0 : p - delta;不要写 max(p - delta, 0)
//   - 不存在的 frame: Remove 静默返回;SetEvictable/RecordAccess 越界抛异常
//   - Remove 不产生 ghost;全新页不进 mfu_,也不碰 p 和 curr_size_
//   - std::list 的 pop_front/pop_back 返回 void,要 front()/back() 先取
// ============================================================================

namespace bustub {

/**
 *
 * TODO(P1): Add implementation
 *
 * @brief a new ArcReplacer, with lists initialized to be empty and target size to 0
 * @param num_frames the maximum number of frames the ArcReplacer will be required to cache
 */
ArcReplacer::ArcReplacer(size_t num_frames) : replacer_size_(num_frames) {}

/**
 * TODO(P1): Add implementation
 *
 * @brief Performs the Replace operation as described by the writeup
 * that evicts from either mfu_ or mru_ into its corresponding ghost list
 * according to balancing policy.
 *
 * If you wish to refer to the original ARC paper, please note that there are
 * two changes in our implementation:
 * 1. When the size of mru_ equals the target size, we don't check
 * the last access as the paper did when deciding which list to evict from.
 * This is fine since the original decision is stated to be arbitrary.
 * 2. Entries that are not evictable are skipped. If all entries from the desired side
 * (mru_ / mfu_) are pinned, we instead try victimize the other side (mfu_ / mru_),
 * and move it to its corresponding ghost list (mfu_ghost_ / mru_ghost_).
 *
 * @return frame id of the evicted frame, or std::nullopt if cannot evict
 */
auto ArcReplacer::Evict() -> std::optional<frame_id_t> 
{ 
    std::scoped_lock lock(latch_);

    if (curr_size_<=0) return std::nullopt;
    auto find_mru = [&]() -> std::optional<frame_id_t>
    {
        for (auto frame:mru_)
        {
            auto it = alive_map_.find(frame);
            if (!it->second->evictable_) continue;
            else
            {
                mru_.remove(frame);
                mru_ghost_.push_back(it->second->page_id_);
                ghost_map_.emplace(it->second->page_id_,std::make_shared<FrameStatus>(it->second->page_id_,it->second->frame_id_,false,ArcStatus::MRU_GHOST));
                alive_map_.erase(it);
                curr_size_--;
                return frame;
            }
        }
        return std::nullopt;
    };
    auto find_mfu = [&]() -> std::optional<frame_id_t>
    {
        for (auto frame=mfu_.rbegin();frame!=mfu_.rend();frame++)
        {
            auto it = alive_map_.find(*frame);
            if (!it->second->evictable_) continue;
            else
            {
                mfu_.remove(*frame);
                mfu_ghost_.push_front(it->second->page_id_);
                ghost_map_.emplace(it->second->page_id_,std::make_shared<FrameStatus>(it->second->page_id_,it->second->frame_id_,false,ArcStatus::MFU_GHOST));
                alive_map_.erase(it);
                curr_size_--;
                return *frame;
            }
        }
        return std::nullopt;
    };
    if (mru_.size()>=mru_target_size_)
    {
        auto victim = find_mru();
        if (victim) return victim;
        return find_mfu();
    }
    else
    {
        auto victim = find_mfu();
        if (victim) return victim;
        return find_mru();
    }

    return std::nullopt;
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Record access to a frame, adjusting ARC bookkeeping accordingly
 * by bring the accessed page to the front of mfu_ if it exists in any of the lists
 * or the front of mru_ if it does not.
 *
 * Performs the operations EXCEPT REPLACE described in original paper, which is
 * handled by `Evict()`.
 *
 * Consider the following four cases, handle accordingly:
 * 1. Access hits mru_ or mfu_
 * 2/3. Access hits mru_ghost_ / mfu_ghost_
 * 4. Access misses all the lists
 *
 * This routine performs all changes to the four lists as preperation
 * for `Evict()` to simply find and evict a victim into ghost lists.
 *
 * Note that frame_id is used as identifier for alive pages and
 * page_id is used as identifier for the ghost pages, since page_id is
 * the unique identifier to the page after it's dead.
 * Using page_id for alive pages should be the same since it's one to one mapping,
 * but using frame_id is slightly more intuitive.
 *
 * @param frame_id id of frame that received a new access.
 * @param page_id id of page that is mapped to the frame.
 * @param access_type type of access that was received. This parameter is only needed for
 * leaderboard tests.
 */
void ArcReplacer::RecordAccess(frame_id_t frame_id, page_id_t page_id, [[maybe_unused]] AccessType access_type) 
{
    std::scoped_lock lock(latch_);
    if (frame_id>=replacer_size_||frame_id<0) throw Exception("Error in Record Access!");
    auto it = alive_map_.find(frame_id);
    // 属于mru
    // 属于mfu
    if (it!=alive_map_.end()) 
    {
        if (it->second->arc_status_==ArcStatus::MRU)
        {
            mru_.remove(frame_id);
            mfu_.push_front(std::move(frame_id));
            it->second->arc_status_ = ArcStatus::MFU;
        }
        else
        {
            mfu_.remove(frame_id);
            mfu_.push_front(std::move(frame_id));
        }
        return;
    }
    // 属于mru_ghost
    // 属于mfu_ghost
    auto git = ghost_map_.find(page_id);
    const size_t b1 = mru_ghost_.size();
    const size_t b2 = mfu_ghost_.size();
    size_t delta = 1;
    if (git!=ghost_map_.end())
    {
        if (git->second->arc_status_==ArcStatus::MRU_GHOST)
        {
            if (b1 != 0&& b1 < b2)
            {
                delta = b2 / b1;
            }
            mru_target_size_ = std::min(mru_target_size_ + delta,replacer_size_);
            mru_ghost_.remove(page_id);
        }
        else
        {
            if (b2!=0&& b2<b1)
            {
                delta = b1/b2;
            }
            mru_target_size_ = (mru_target_size_ < delta)? 0: (mru_target_size_ - delta);
            mfu_ghost_.remove(page_id);
        }
        mfu_.push_front(frame_id);
        alive_map_.emplace(frame_id,std::make_shared<FrameStatus>(page_id,frame_id,false,ArcStatus::MFU));
        ghost_map_.erase(git);
        return;
    }
    // 都不属于
    if (mru_.size()+mru_ghost_.size()==replacer_size_)
    {
        if (mru_.size()<replacer_size_&&!mru_ghost_.empty())
        {
           page_id_t victim_page = mru_ghost_.front();
           mru_ghost_.pop_front();
           ghost_map_.erase(victim_page);
        }
    }
    else if (mru_.size()+mru_ghost_.size()+mfu_.size()+mfu_ghost_.size()==2*replacer_size_)
    {
        if (!mfu_ghost_.empty())
        {
            page_id_t victim_page = mfu_ghost_.back();
            mfu_ghost_.pop_back();
            ghost_map_.erase(victim_page);
        }
    }

    alive_map_.emplace(frame_id,std::make_shared<FrameStatus>(page_id,frame_id,false,ArcStatus::MRU));
    mru_.push_back(frame_id);
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Toggle whether a frame is evictable or non-evictable. This function also
 * controls replacer's size. Note that size is equal to number of evictable entries.
 *
 * If a frame was previously evictable and is to be set to non-evictable, then size should
 * decrement. If a frame was previously non-evictable and is to be set to evictable,
 * then size should increment.
 *
 * If frame id is invalid, throw an exception or abort the process.
 *
 * For other scenarios, this function should terminate without modifying anything.
 *
 * @param frame_id id of frame whose 'evictable' status will be modified
 * @param set_evictable whether the given frame is evictable or not
 */
void ArcReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) 
{
    std::scoped_lock lock(latch_);
    auto it = alive_map_.find(frame_id);
    if(frame_id>=replacer_size_||frame_id<0)
    {
        throw Exception("Invalid frame ID, please use another frame id.");
    }
    if (it==alive_map_.end()||it->second->evictable_==set_evictable) return;
    it->second->evictable_ = set_evictable;
    set_evictable? curr_size_++:curr_size_--;
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Remove an evictable frame from replacer.
 * This function should also decrement replacer's size if removal is successful.
 *
 * Note that this is different from evicting a frame, which always remove the frame
 * decided by the ARC algorithm.
 *
 * If Remove is called on a non-evictable frame, throw an exception or abort the
 * process.
 *
 * If specified frame is not found, directly return from this function.
 *
 * @param frame_id id of frame to be removed
 */
void ArcReplacer::Remove(frame_id_t frame_id) 
{
    std::scoped_lock lock(latch_);

    auto it = alive_map_.find(frame_id);
    if (it==alive_map_.end()) return;
    if (!it->second->evictable_)
    {
        throw Exception("The frame is non-evitable!");
    }
    if (it->second->arc_status_==ArcStatus::MFU)
    {
        alive_map_.erase(it);
        mfu_.remove(frame_id);
    }
    else
    {
        alive_map_.erase(it);
        mru_.remove(frame_id);
    }

    curr_size_--;
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Return replacer's size, which tracks the number of evictable frames.
 *
 * @return size_t
 */
auto ArcReplacer::Size() -> size_t 
{ 
    std::scoped_lock lock(latch_);
    return curr_size_;
}

}  // namespace bustub
