extern "C" {
#include <base/assert.h>
}

#include "helpers.hpp"
#include "internal/ds_info.hpp"
#include "list.hpp"
#include "manager.hpp"

namespace far_memory {

GenericList::GenericList(const DerefScope &scope, const uint16_t kItemSize,
                         const uint16_t kNumNodesPerChunk, bool enable_merge,
                         bool customized_split)
    // kItemSize_ ： 一个元素的大小
    // kNumNodesPerChunk_： 一个chunk可以保存多少个元素
    : kItemSize_(kItemSize), kNumNodesPerChunk_(kNumNodesPerChunk),
    // kChunkListNodeSize_： 在chunk中一个元素的大小（因为要给每个元素增加一个链表的next和prev，所以和元素本身大小不同）
      kChunkListNodeSize_(sizeof(ChunkList::Node) + kItemSize),
    // kChunkSize_： 一个chunk的需要的空间大小（包含chunk头的元数据、该chunk内的所有元素的链表元数据、每个元素和它的next/prev）
      kChunkSize_(sizeof(ChunkListData) + sizeof(ChunkList::ListData) +
                  kNumNodesPerChunk * kChunkListNodeSize_),
      kInitMeta_(((~static_cast<decltype(kInitMeta_)>(0)) >>
                  (8 * sizeof(kInitMeta_) - kNumNodesPerChunk))),
    // kMergeThresh_： 触发合并的阈值（小于kMergeThresh_个元素时进行合并）
      kMergeThresh_(
          static_cast<uint16_t>(kNumNodesPerChunk_ * kMergeThreshRatio)),
      kPrefetchNumNodes_(
          FarMemManagerFactory::get()->get_device()->get_prefetch_win_size() /
          kChunkSize_),
      enable_merge_(enable_merge), customized_split_(customized_split) {
    // 为该list增加两个初始的LocalNode用于初始Chunk的信息 
  local_list_.push_back(LocalNode());
  local_list_.push_back(LocalNode());
    // 为初始的两个chunk申请两个初始Chunk，用于保存初始的少量数据
  init_local_node(scope, &local_list_.front());
  init_local_node(scope, &local_list_.back());
    // 将初始chunk中的元素数量设置为 kInvalidCnt = Max + 1 ，表示该chunk中目前还没有保存元素
  local_list_.front().cnt = kInvalidCnt;
  local_list_.back().cnt = kInvalidCnt;
}

// 申请一个Chunk保存list中的多个（8-64）个元素，并将该Chunk的信息保存在local_node中
void GenericList::init_local_node(const DerefScope &scope,
                                  LocalNode *local_node) {
  local_node->ptr.nullify();
  // 为chunk申请空间，用一个unique_ptr指向该空间
  // 一个chunk的分布为：
  //   /--ChunkListData--/--ChunkList::ListData--/--GenericLocalListNode--/
  //   /--meta--/--head--tail--head_ptr--tail_ptr--/--Object data--/
  local_node->ptr =
      std::move(FarMemManagerFactory::get()->allocate_generic_unique_ptr(
          kVanillaPtrDSID, kChunkSize_));
  
    // 申请空间的开头为 ChunkListData
  auto *chunk_list_data =
      static_cast<ChunkListData *>(local_node->ptr.deref_mut(scope));
  chunk_list_data->meta = kInitMeta_;

    // 第二项为 ChunkList::ListData
  auto list_data =
      reinterpret_cast<ChunkList::ListData *>(chunk_list_data->data);
  
  //为chunk申请一个ChunkList保存，保存在local_node的chunk_list里面
  auto *chunk_list_ptr = &(local_node->chunk_list);
  struct ChunkListState state;
  state.list_data_ptr_addr =
      reinterpret_cast<uint64_t>(&(chunk_list_ptr->list_data_));
  state.kChunkListNodeSize = kChunkListNodeSize_;
  //q:应该是把新创建的ChunkList赋给chunk_list_ptr
  new (chunk_list_ptr) ChunkList(list_data, state);

    //第三项为GenericLocalListNode
  auto base_addr = reinterpret_cast<uint64_t>(chunk_list_data) +
                   static_cast<uint64_t>(sizeof(ChunkListData));
  auto head_addr = reinterpret_cast<uint64_t>(&(list_data->head));
  auto tail_addr = reinterpret_cast<uint64_t>(&(list_data->tail));
  chunk_list_ptr->init(ChunkNodePtr(0, head_addr - base_addr),
                       ChunkNodePtr(0, tail_addr - base_addr));
}

} // namespace far_memory
