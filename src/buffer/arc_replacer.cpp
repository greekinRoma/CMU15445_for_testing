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
// 在可驱逐的活页中挑选一个受害者，把它的page_id写进对应ghost写进ghost，返回它的frame_id，一个挑选不出来就返回null
auto ArcReplacer::Evict() -> std::optional<frame_id_t> 
{ 
    std::scoped_lock lock(latch_);

    if (curr_size_ == 0 )
    {
        return std::nullopt;
    }
    const bool try_mru_first = mru_.size() >= mru_target_size_;

    auto find_in_mru = [&]() -> std::optional<frame_id_t>
    {
        for (frame_id_t frame:mru_)
        {
            auto it = alive_map_.find(frame);
            if (it!=alive_map_.end()&&it->second->evictable_) return frame;  
        }
        return std::nullopt;
    };

    auto find_in_mfu = [&]() ->std::optional<frame_id_t>
    {
        for(auto rit=mfu_.rbegin();rit!=mfu_.rend();++rit)
        {
        auto it = alive_map_.find(*rit);
        if (it !=alive_map_.end()&& it->second->evictable_)
        {
            return *rit;
        }
        }
        return std::nullopt;
    };

    std::optional<frame_id_t> victim;
    bool from_mru = try_mru_first;
    if (try_mru_first)
    {
        victim = find_in_mru();
        if (!victim)
        {
            victim =find_in_mfu();
            from_mru = false;
        }
    }
    else
    {
        victim = find_in_mfu();
        if (!victim)
        {
            victim = find_in_mru();
            from_mru = true;
        }
    }

    if (!victim) return std::nullopt;

    auto st =alive_map_.find(*victim);
    const page_id_t pid = st->second->page_id_;
    alive_map_.erase(st);

    if (from_mru)
    {
        mru_.remove(*victim);
        mru_ghost_.push_back(pid);
        ghost_map_.emplace(pid,std::make_shared<FrameStatus>(pid, *victim, false, ArcStatus::MRU_GHOST));
    }
    else
    {
        mfu_.remove(*victim);
        mfu_ghost_.push_front(pid);
        ghost_map_.emplace(pid,std::make_shared<FrameStatus>(pid, *victim, false, ArcStatus::MFU_GHOST));
    }
    curr_size_ --;
    return victim;
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
    if (frame_id >= static_cast<frame_id_t>(replacer_size_)||frame_id<0)
    {
        throw Exception("Error in frame id.");
    }
    auto it = alive_map_.find(frame_id);
    if (it != alive_map_.end())
    {
        if(it->second->arc_status_== ArcStatus::MFU)
        {
            mfu_.remove(frame_id);
            mfu_.emplace_front(frame_id);
        }
        else
        {
            mru_.remove(frame_id);
            mfu_.emplace_front(frame_id);
            it->second->arc_status_ = ArcStatus::MFU;
        }
        return;
    }
    auto git = ghost_map_.find(page_id);
    // std::unordered_map<frame_id_t, std::shared_ptr<FrameStatus>>::iterator 是 it的类型
    if (git!=ghost_map_.end())
    {
        const size_t b1 = mru_ghost_.size();
        const size_t b2 = mfu_ghost_.size();
        const size_t c = replacer_size_;

        if (git->second->arc_status_==ArcStatus::MRU_GHOST)
        {
            mru_ghost_.remove(page_id);
            const size_t delta = (b1>=b2)? 1:(b2/b1);
            mru_target_size_ = std::min(mru_target_size_+delta,c);
        }
        else
        {
            mfu_ghost_.remove(page_id);   
            const size_t delta = (b2>=b1)? 1:(b1/b2);
            mru_target_size_ = (mru_target_size_<delta)? 0:mru_target_size_-delta;
        }
        ghost_map_.erase(git);
        mfu_.push_front(frame_id);
        alive_map_.emplace(frame_id,std::make_shared<FrameStatus>(FrameStatus(page_id, frame_id, false, ArcStatus::MFU)));
        return;
        // 活页永远不会超，我们只需要管ghost_page
    }
    // alive_map_.emplace(frame_id,std::make_shared<FrameStatus>(FrameStatus(page_id, frame_id, false, ArcStatus::MRU)));
    // mru_.push_back(frame_id);
    // if (mru_.size() + mru_ghost_.size() == replacer_size_)
    // {
    //     page_id_t victim_page = mru_ghost_.front();
    //     mru_ghost_.pop_front();
    //     ghost_map_.erase(victim_page);
    // }
    // else if
    if (mru_.size()+mru_ghost_.size() == replacer_size_)
    {
        if(mru_.size()<replacer_size_ && !mru_ghost_.empty())
        {
            page_id_t victim_page = mru_ghost_.front();
            mru_ghost_.pop_front();
            ghost_map_.erase(victim_page);
        }
    }
    else if (mru_.size() + mfu_.size()+mfu_ghost_.size()+mru_ghost_.size()>=2*replacer_size_)
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
//知识点
/**
 * 堆（Heap）与栈（Stack）的内存管理机制：
 *
 * 1. 栈（Stack）
 *    每个线程通常拥有独立的栈空间，由操作系统和线程运行库分配。
 *    栈主要用于存储函数调用信息、局部变量等。
 *    局部对象在作用域结束时自动销毁，栈空间可以被后续函数复用。
 *    线程结束并完成资源回收后，其栈空间被回收。
 *
 * 2. 堆（Heap）
 *    堆属于进程的动态内存空间，可以被同一进程中的多个线程访问。
 *    堆对象的生命周期不直接依赖于创建它的函数或线程。
 *    堆内存的回收方式取决于其分配方式：
 *      - new         : 通过 delete 手动释放；
 *      - malloc      : 通过 free 手动释放；
 *      - make_unique : 由唯一所有者自动管理；
 *      - make_shared : 由共享所有者通过引用计数自动管理。
 *    释放的内存通常先归还给内存分配器，不一定立即归还操作系统。
 *    进程结束时，操作系统会回收该进程占用的内存资源。
 *
 * 3. std::make_shared<FrameStatus>()
 *    在堆上创建 FrameStatus 对象及其控制块，并返回 shared_ptr。
 *    初始共享引用计数为 1，复制智能指针会增加引用计数，
 *    智能指针销毁或 reset() 会减少引用计数。
 *    当共享引用计数归零时，FrameStatus 对象自动销毁。
 *
 * 4. 在 ArcReplacer 中的作用
 *    局部 shared_ptr 通常存储于当前线程的栈中，
 *    而其指向的 FrameStatus 对象存储于堆中。
 *    将 shared_ptr 复制至 alive_map_ 或 ghost_map_ 后，
 *    即使当前函数或线程结束，只要哈希表仍持有该对象，
 *    FrameStatus 就不会被销毁。
 *
 *    注意：shared_ptr 只保证共享所有权的安全管理，
 *    不保证其指向对象的并发读写安全，仍需使用 mutex 等同步机制。
 *
 * 总结：
 *    栈空间的生命周期取决于线程；
 *    栈中局部对象的生命周期取决于作用域；
 *    堆中动态对象的生命周期取决于所有权或显式释放操作。
 */
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
    if (frame_id >= static_cast<frame_id_t>(replacer_size_)||frame_id<0)
    {
        throw Exception("Error in frame id.");
    }
    auto it = alive_map_.find(frame_id);
    if (it==alive_map_.end()||it->second->evictable_ == set_evictable) return;
    set_evictable? curr_size_++: curr_size_--;
    it->second->evictable_ = set_evictable;
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
        throw Exception("frame_id Error!");
    }
    if (it->second->arc_status_ == ArcStatus::MRU)
    {
        mru_.remove(frame_id);
    }
    else
    {
        mfu_.remove(frame_id);
    }
    alive_map_.erase(it);
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
    // return 0; 
    std::scoped_lock lock(latch_);
    return curr_size_;
}

}  // namespace bustub
