#pragma once

extern "C" {
#include <runtime/preempt.h>
}

#include "helpers.hpp"

#include <memory>
#include <stack>
#include <type_traits>
#include <vector>

namespace far_memory {

template <typename T> class LocalList;

// GenericLocalListNode是一个List类型的一个元素
// next和prev指向的是下一个和上一个GenericLocalListNode的首地址！
// data部分保存当前元素的数据，除了真实数据，LocalNode也是以这种格式保存的，
// 而LocalNode中保存的并不是真实数据，而是Chunk的元数据信息
// data使用柔性数组，当GenericLocalListNode放在LocalList中申请的时候，
// 根据传入的变量类型T为data申请不同大小的空间，其中T有可能是用户数据，也可能是LocalNode中的元数据信息
template <typename NodePtr> struct GenericLocalListNode {
  NodePtr next;
  NodePtr prev;
  uint8_t data[0];
};

#pragma pack(push, 1)
// GenericLocalListData为GenericLocalListNode的头
// 一般放置在一串GenericLocalListNode前保存该链表的一些元数据信息
// data部分一般就是GenericLocalListNode
template <typename NodePtr> struct GenericLocalListData {
  GenericLocalListNode<NodePtr> head;
  GenericLocalListNode<NodePtr> tail;
  NodePtr head_ptr;
  NodePtr tail_ptr;
  uint8_t data[0];
};
#pragma pack(pop)

// GenericLocalList就是一个chunk链表的封装
// 只是对GenericLocalListData进行了一定的封装，没有真实数据，只有指针
template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
class GenericLocalList {
private:
  using Node = GenericLocalListNode<NodePtr>;
  using ListData = GenericLocalListData<NodePtr>;
  using DerefFnTraits = helpers::FunctionTraits<DerefFn>;
  using AllocateFnTraits = helpers::FunctionTraits<AllocateFn>;
  using FreeFnTraits = helpers::FunctionTraits<FreeFn>;
  using FnStateType = typename DerefFnTraits::template Arg<1>::Type;
  constexpr static bool kFnHasState = (DerefFnTraits::Arity == 2);
  using StateType =
      std::conditional<kFnHasState, FnStateType, uint8_t[0]>::type;

  // DerefFn: (NodePtr, [optional]FnStateType)->Node &
  static_assert(DerefFnTraits::Arity == 1 || DerefFnTraits::Arity == 2);
  static_assert(std::is_same<
                NodePtr, typename DerefFnTraits::template Arg<0>::Type>::value);
  static_assert(
      std::is_same<FnStateType,
                   typename DerefFnTraits::template Arg<1>::Type>::value);
  static_assert(
      std::is_same<Node &, typename DerefFnTraits::ResultType>::value);

  // AllocateFn: ([optional]FnStateType)->NodePtr
  static_assert(AllocateFnTraits::Arity + 1 == DerefFnTraits::Arity);
  static_assert(
      std::is_same<FnStateType,
                   typename AllocateFnTraits::template Arg<0>::Type>::value);
  static_assert(
      std::is_same<NodePtr, typename AllocateFnTraits::ResultType>::value);

  // FreeFn: (NodePtr, [optional]FnStateType)->void
  static_assert(static_cast<int>(FreeFnTraits::Arity) == DerefFnTraits::Arity);
  static_assert(std::is_same<
                NodePtr, typename FreeFnTraits::template Arg<0>::Type>::value);
  static_assert(
      std::is_same<FnStateType,
                   typename FreeFnTraits::template Arg<1>::Type>::value);
  static_assert(std::is_same<void, typename FreeFnTraits::ResultType>::value);

  template <bool Reverse> class IteratorImpl {
  private:
    // ptr_保存的是GenericLocalListNode的指针，可以直接指针转化后使用
    NodePtr ptr_;
    StateType state_;

    friend class GenericLocalList;
    friend class GenericList;
    template <typename T> friend class LocalList;

    Node &deref(NodePtr ptr) const;
    uint8_t *insert();
    // 从链表中删除当前元素，返回下一个或上一个元素
    // 当前元素的data指针将被保存在*data_ptr中
    IteratorImpl<Reverse> erase(uint8_t **data_ptr);
    NodePtr allocate();
    void free(NodePtr node_ptr);

  public:
    IteratorImpl();
    IteratorImpl(NodePtr ptr, StateType state);
    template <bool OReverse> IteratorImpl(const IteratorImpl<OReverse> &o);
    template <bool OReverse>
    IteratorImpl &operator=(const IteratorImpl<OReverse> &o);
    // 前缀加 ++I;
    IteratorImpl &operator++();
    // 后缀加 I++;
    IteratorImpl operator++(int);
    IteratorImpl &operator--();
    IteratorImpl operator--(int);
    bool operator==(const IteratorImpl &o) const;
    bool operator!=(const IteratorImpl &o) const;
    uint8_t *operator*() const;
  };

  using Iterator = IteratorImpl</* Reverse */ false>;
  using ReverseIterator = IteratorImpl</* Reverse */ true>;

  GenericLocalList();

  ListData *list_data_;
  StateType state_;
  friend class GenericList;
  template <typename T> friend class LocalList;
  template <typename T> friend class List;

public:
  GenericLocalList(ListData *list_data);
  GenericLocalList(ListData *list_data, StateType state);
  void init(NodePtr head_ptr, NodePtr tail_ptr);
  void set_list_data(ListData *list_data);
  Iterator begin() const;
  Iterator end() const;
  ReverseIterator rbegin() const;
  ReverseIterator rend() const;
  template <bool Reverse> uint8_t *insert(const IteratorImpl<Reverse> &iter);
  template <bool Reverse>
  IteratorImpl<Reverse> erase(const IteratorImpl<Reverse> &iter,
                              uint8_t **data_ptr);
};

// LocalList中保存了当前节点申请的list的Node信息
// 创建了一个Node的池，保存了真实的Node的数据信息（Node申请内存空间在这里进行）
// 不像GenericLocalListData只有指针，不关心内存空间从哪里申请出来的
template <typename T> class LocalList {
private:
  using NodePtr = uint8_t *;
  using NodePool = std::stack<NodePtr, std::vector<NodePtr>>;
  using AutoNodeCleaner = std::vector<std::unique_ptr<uint8_t>>;

  constexpr static uint32_t kReplenishNumNodes = 8192;

  // 将GenericLocalListNode的首地址转化成GenericLocalListNode对象返回
  static GenericLocalListNode<NodePtr> &deref_node_ptr(NodePtr ptr,
                                                       NodePool *node_pool);
  // 从Node Pool中取一个Node返回首地址
  static NodePtr allocate_node(NodePool *node_pool);
  // 将该Node放回Node Pool，并调用ptr指向的T的析构函数释放空间
  static void free_node(NodePtr ptr, NodePool *node_pool);

  constexpr static auto kDerefNodePtrFn =
      [](NodePtr ptr, NodePool *node_pool) -> GenericLocalListNode<NodePtr> & {
    return deref_node_ptr(ptr, node_pool);
  };
  constexpr static auto kAllocateNodeFn = [](NodePool *node_pool) -> NodePtr {
    return allocate_node(node_pool);
  };
  constexpr static auto kFreeNodeFn = [](NodePtr ptr, NodePool *node_pool) {
    return free_node(ptr, node_pool);
  };
  using TGenericLocalList =
      GenericLocalList<NodePtr, decltype(kDerefNodePtrFn),
                       decltype(kAllocateNodeFn), decltype(kFreeNodeFn)>;

  template <bool Reverse> class IteratorImpl {
  private:
    TGenericLocalList::template IteratorImpl<Reverse> generic_iter_;
    friend class LocalList;

  public:
    IteratorImpl();
    template <bool OReverse>
    IteratorImpl(
        TGenericLocalList::template IteratorImpl<OReverse> generic_iter);
    template <bool OReverse> IteratorImpl(const IteratorImpl<OReverse> &o);
    template <bool OReverse>
    IteratorImpl &operator=(const IteratorImpl<OReverse> &o);
    IteratorImpl &operator++();
    IteratorImpl operator++(int);
    IteratorImpl &operator--();
    IteratorImpl operator--(int);
    bool operator==(const IteratorImpl &o) const;
    bool operator!=(const IteratorImpl &o) const;
    T &operator*() const;
    T *operator->() const;
  };

  using Iterator = IteratorImpl</* Reverse */ false>;
  using ReverseIterator = IteratorImpl</* Reverse */ true>;

  // 保存每个Node的vector，以stack栈的形式保存
  NodePool node_pool_;
  // 保存每次增加Node时申请到的首地址
  // 每个首地址代表了kReplenishNumNodes * kNodeSize的连续地址空间用于保存类型为T的多个对象
  AutoNodeCleaner auto_cleaner_;
  TGenericLocalList::ListData list_data_;
  TGenericLocalList generic_local_list_;
  uint64_t size_ = 0;
  friend class GenericList;

public:
  ~LocalList();
  LocalList();
  Iterator begin() const;
  Iterator end() const;
  ReverseIterator rbegin() const;
  ReverseIterator rend() const;
  T &front() const;
  T &back() const;
  uint64_t size() const;
  bool empty() const;
  void push_front(const T &data);
  void pop_front();
  void push_back(const T &data);
  void pop_back();
  template <bool Reverse>
  void insert(const IteratorImpl<Reverse> &iter, const T &data);
  template <bool Reverse>
  IteratorImpl<Reverse> erase(const IteratorImpl<Reverse> &iter);
};

} // namespace far_memory

#include "internal/local_list.ipp"
