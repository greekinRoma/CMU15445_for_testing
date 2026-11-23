//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.cpp
//
// Identification: src/buffer/lru_k_replacer.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/lru_k_replacer.h"
#include "common/config.h"
#include "common/exception.h"
// 它实现了 LRU-K 页面替换(淘汰)策略,是缓冲池的"选受害者"决策器:当缓冲池满了、需要腾出空间读入新页面时,由它决定把哪个 frame(内存帧)里的页面换出去。LRU 只看"最近一次访问",容易被"相关引用"(correlated reference)污染——比如一次顺序扫描把每个页面都碰了一下,导致真正热门的页面被挤出。LRU-K 保留每个 frame 最近 K 次访问的时间戳,看的是"第 K 次最近访问距现在多远"(backward k-distance),离得越远说明越久没被频繁使用,越该淘汰。K=1 就退化成普通 LRU,常用的是 K=2。
namespace bustub {

/**
 *
 * TODO(P1): Add implementation
 *
 * @brief a new LRUKReplacer.
 * @param num_frames the maximum number of frames the LRUReplacer will be required to store
 */
LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : replacer_size_(num_frames), k_(k) {}
// 构造函数，num_frames = 缓冲池帧数上限
/**
 * TODO(P1): Add implementation
 *
 * @brief Find the frame with largest backward k-distance and evict that frame. Only frames
 * that are marked as 'evictable' are candidates for eviction.
 *
 * A frame with less than k historical references is given +inf as its backward k-distance.
 * If multiple frames have inf backward k-distance, then evict frame whose oldest timestamp
 * is furthest in the past.
 *
 * Successful eviction of a frame should decrement the size of replacer and remove the frame's
 * access history.
 *
 * @return the frame ID if a frame is successfully evicted, or `std::nullopt` if no frames can be evicted.
 */
auto LRUKReplacer::Evict() -> std::optional<frame_id_t> {
    std::scoped_lock lock(latch_);
    std::optional<frame_id_t>  best_inf;
    std::optional<frame_id_t> best_finite;
    size_t best_inf_ts = 0;
    size_t best_finite_ts = 0;
    if (curr_size_<1)
    {
        return std::nullopt;
    }
    for (auto it:node_store_)
    {
        if (it.second.Is_Evictable()) 
        {
            if (it.second.Get_Size()<k_)
            {
                if (!best_inf.has_value()){best_inf = it.first;best_inf_ts = it.second.history_.front();}
                else if (best_inf_ts>it.second.history_.front())
                {
                    /* code */
                    best_inf_ts = it.second.history_.front();
                    best_inf = it.first;
                }
                
            }
            else
            {
                if (!best_finite.has_value())
                {
                    best_finite = it.first;
                    best_finite_ts = it.second.history_.front();
                }
                else if (best_finite_ts>it.second.history_.front())
                {
                    best_finite_ts = it.second.history_.front();
                    best_finite = it.first;
                }

            }
        }
    }
    frame_id_t victim = best_inf.has_value() ? *best_inf : *best_finite;
    node_store_.erase(victim);
    curr_size_--;
    return victim;

  // best_inf表示可以未null
  // best_inf 是可选择的，其中没有初始化返回是false
  //  history.front() < best_inf_ts 表示最早优先
  //有best_inf先选
  // * 这个从std::optional<frame_id_t>取出idx
//   UNIMPLEMENTED("TODO(P1): Add implementation.");

}
// 挑一个受害者换出去。在所有可驱逐的frame中找打backward k-distance最大的；不足k次历史的按最首次访问比较。只负责把该frame是BuffPoolManager的事.
/**
 * TODO(P1): Add implementation
 *
 * @brief Record the event that the given frame id is accessed at current timestamp.
 * Create a new entry for access history if frame id has not been seen before.
 *
 * If frame id is invalid (ie. larger than replacer_size_), throw an exception. You can
 * also use BUSTUB_ASSERT to abort the process if frame id is invalid.
 *
 * @param frame_id id of frame that received a new access.
 * @param access_type type of access that was received. This parameter is only needed for
 * leaderboard tests.
 */
void LRUKReplacer::RecordAccess(frame_id_t frame_id, AccessType access_type) {
  // emplace构造一起放进去
   std::scoped_lock lock(latch_);
   if (frame_id>=static_cast<frame_id_t>(replacer_size_)||frame_id<0)
   {
    throw Exception("frame_id Error!");
   }
   auto it = node_store_.find(frame_id);
   if (it == node_store_.end()) 
   {
    it = node_store_.try_emplace(frame_id, k_, frame_id).first;
    // cur_state 是总共可以编译的页面的数目，但是新生成的页面都是不可编辑的
   }
   current_timestamp_  = current_timestamp_ +1;
   it->second.Push_History(current_timestamp_);
}
//记录一次访问。缓冲池每次读写页面都要调它:时间戳 +1,把当前时间戳追加到该 frame 的访问历史尾部,超过 k 个就丢掉最老的那个(只保留最近 k 次)。第一次见到的 frame 会自动创建历史。access_type 只用于 leaderboard 测试,基础实现可忽略。
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
void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
    std::scoped_lock lock(latch_);
    if (frame_id >=static_cast<frame_id_t>(replacer_size_)||frame_id<0)
    {
        throw Exception("Error in frame_id");
    }
    auto it = node_store_.find(frame_id);
    if (it== node_store_.end()||it->second.Is_Evictable() == set_evictable) return;
    it->second.Set_Evictable(set_evictable);
    set_evictable? curr_size_++:curr_size_--;

  // replacer_size 缓冲池总帧数
  // curr_size_ 是可以被驱逐的数据
//   UNIMPLEMENTED("TODO(P1): Add implementation.");

}
// 设置该 frame 是否允许被驱逐。被 pin 住的页面(pin_count>0)必须标记为不可驱逐,这样 Evict 永远不会选到它。它同时维护 curr_size_(可驱逐数量):true→+1,false→−1。
/**
 * TODO(P1): Add implementation
 *
 * @brief Remove an evictable frame from replacer, along with its access history.
 * This function should also decrement replacer's size if removal is successful.
 *
 * Note that this is different from evicting a frame, which always remove the frame
 * with largest backward k-distance. This function removes specified frame id,
 * no matter what its backward k-distance is.
 *
 * If Remove is called on a non-evictable frame, throw an exception or abort the
 * process.
 *
 * If specified frame is not found, directly return from this function.
 *
 * @param frame_id id of frame to be removed
 */
void LRUKReplacer::Remove(frame_id_t frame_id) 
{ 
   std::scoped_lock lock(latch_);
   if (frame_id>=static_cast<frame_id_t>(replacer_size_)||frame_id<0)
   {
    throw Exception("frame_id Error!");
   }
   auto it = node_store_.find(frame_id);
   if (it == node_store_.end()) return;
   if (!it->second.Is_Evictable()) throw Exception("Remove called on a non-evictable frame");
   curr_size_--;
   node_store_.erase(it);
}
// 定点删除。和 Evict 的"按策略挑人"不同,它删指定的 frame(比如 BPM 里 DeletePage 要把某页直接从内存移除)。frame 不可驱逐时调用要抛异常;不存在则静默返回。
/**
 * TODO(P1): Add implementation
 *
 * @brief Return replacer's size, which tracks the number of evictable frames.
 *
 * @return size_t
 */
auto LRUKReplacer::Size() -> size_t { 
    std::scoped_lock lock(latch_);
    return curr_size_;
}
// 	返回 curr_size_——注意是可驱逐帧的数量,不是被跟踪的节点总数(这正是被 pin 的页面不算数的原因)。
}  // namespace bustub
