#pragma once

#include <cstring>

namespace far_memory {

template <typename T>
FORCE_INLINE GenericLocalListNode<typename LocalList<T>::NodePtr> &
LocalList<T>::deref_node_ptr(NodePtr ptr, NodePool *node_pool) {
  return *reinterpret_cast<GenericLocalListNode<NodePtr> *>(ptr);
}

template <typename T>
FORCE_INLINE LocalList<T>::NodePtr
LocalList<T>::allocate_node(NodePool *node_pool) {
  // 如果node_pool为空，则申请新的空Node填入node_pool
  if (unlikely(node_pool->empty())) {
    // 禁止中断
    preempt_disable();
    // node_pool和auto_cleaner_是连续申请的，所以可以直接通过地址取到
    auto auto_node_cleaner = reinterpret_cast<AutoNodeCleaner *>(
        reinterpret_cast<uint64_t>(node_pool) + sizeof(NodePool));
    // Node由GenericLocalListNode<uint8_t *>和对象T本身组成
    // 因为GenericLocalListNode使用的是柔性数组，所以其实Node就是填充了数据的GenericLocalListNode
    constexpr auto kNodeSize =
        sizeof(GenericLocalListNode<NodePtr>) + sizeof(T);
    auto mem =
        reinterpret_cast<uint8_t *>(malloc(kReplenishNumNodes * kNodeSize));
    auto_node_cleaner->push_back(std::unique_ptr<uint8_t>(mem));
    auto node = reinterpret_cast<NodePtr>(mem);
    for (uint64_t i = 0; i < kReplenishNumNodes; i++) {
      node_pool->push(node);
      node += kNodeSize;
    }
    preempt_enable();
  }
  auto ret = node_pool->top();
  node_pool->pop();
  return ret;
}

template <typename T>
FORCE_INLINE void LocalList<T>::free_node(NodePtr ptr, NodePool *node_pool) {
  node_pool->push(ptr);
  reinterpret_cast<T *>(ptr + sizeof(GenericLocalListNode<NodePtr>))->~T();
}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
template <bool Reverse>
FORCE_INLINE GenericLocalList<NodePtr, DerefFn, AllocateFn,
                              FreeFn>::IteratorImpl<Reverse>::IteratorImpl() {}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
template <bool Reverse>
FORCE_INLINE
GenericLocalList<NodePtr, DerefFn, AllocateFn,
                 FreeFn>::IteratorImpl<Reverse>::IteratorImpl(NodePtr ptr,
                                                              StateType state)
    : ptr_(ptr), state_(state) {}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
template <bool Reverse>
template <bool OReverse>
FORCE_INLINE
GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::IteratorImpl<
    Reverse>::IteratorImpl(const IteratorImpl<OReverse> &o)
    : ptr_(o.ptr_), state_(o.state_) {}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
template <bool Reverse>
template <bool OReverse>
FORCE_INLINE GenericLocalList<NodePtr, DerefFn, AllocateFn,
                              FreeFn>::template IteratorImpl<Reverse> &
    GenericLocalList<NodePtr, DerefFn, AllocateFn,
                     FreeFn>::template IteratorImpl<Reverse>::
    operator=(const IteratorImpl<OReverse> &o) {
  this->ptr_ = o.ptr_;
  this->state_ = o.state_;
  return *this;
}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
template <bool Reverse>
// 返回值为GenericLocalListNode<NodePtr> &
FORCE_INLINE GenericLocalListNode<NodePtr> &
// 函数名为下面两行
GenericLocalList<NodePtr, DerefFn, AllocateFn,
                 FreeFn>::IteratorImpl<Reverse>::deref(NodePtr ptr) const {
  // q&a:为什么这里直接定义了一个新的kDerefNodePtrFn就可以用啊?
  // 因为这里传进来的 DerefFn 是lambda函数，所以传进来的类型底层是一个struct，
  // struct内部会有成员操作符 () ，指代的就是lambda函数本身，所以可以直接使用。
  // 这是c++20才支持的特性。
  const DerefFn kDerefNodePtrFn;
  if constexpr (kFnHasState) {
    return kDerefNodePtrFn(ptr, state_);
  } else {
    return kDerefNodePtrFn(ptr);
  }
}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
template <bool Reverse>
FORCE_INLINE NodePtr GenericLocalList<
    NodePtr, DerefFn, AllocateFn, FreeFn>::IteratorImpl<Reverse>::allocate() {
  const AllocateFn kAllocateNodeFn;
  if constexpr (kFnHasState) {
    return kAllocateNodeFn(state_);
  } else {
    return kAllocateNodeFn();
  }
}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
template <bool Reverse>
FORCE_INLINE void
GenericLocalList<NodePtr, DerefFn, AllocateFn,
                 FreeFn>::IteratorImpl<Reverse>::free(NodePtr node_ptr) {
  const FreeFn kFreeNodeFn;
  if constexpr (kFnHasState) {
    return kFreeNodeFn(node_ptr, state_);
  } else {
    return kFreeNodeFn(node_ptr);
  }
}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
template <bool Reverse>
FORCE_INLINE uint8_t *
GenericLocalList<NodePtr, DerefFn, AllocateFn,
                 FreeFn>::IteratorImpl<Reverse>::insert() {
  // 当前节点
  auto *node = &deref(ptr_);
  // 申请一个新的节点
  auto new_node_ptr = allocate();
  // 解引用
  auto *new_node = &deref(new_node_ptr);

  if constexpr (Reverse) {
    new_node->next = node->next;
    auto *next_node = &deref(node->next);
    next_node->prev = new_node_ptr;
    new_node->prev = ptr_;
    node->next = new_node_ptr;
  } else {
    new_node->prev = node->prev;
    auto *prev_node = &deref(node->prev);
    prev_node->next = new_node_ptr;
    new_node->next = ptr_;
    node->prev = new_node_ptr;
  }
  return reinterpret_cast<uint8_t *>(new_node->data);
}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
template <bool Reverse>
FORCE_INLINE GenericLocalList<NodePtr, DerefFn, AllocateFn,
                              FreeFn>::template IteratorImpl<Reverse>
GenericLocalList<NodePtr, DerefFn, AllocateFn,
                 FreeFn>::IteratorImpl<Reverse>::erase(uint8_t **data_ptr) {
  auto &node = deref(ptr_);
  *data_ptr = reinterpret_cast<uint8_t *>(node.data);
  auto &prev_node = deref(node.prev);
  prev_node.next = node.next;
  auto &next_node = deref(node.next);
  next_node.prev = node.prev;
  free(ptr_);
  if constexpr (Reverse) {
    return IteratorImpl<Reverse>(node.prev, state_);
  } else {
    return IteratorImpl<Reverse>(node.next, state_);
  }
}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
template <bool Reverse>
FORCE_INLINE GenericLocalList<NodePtr, DerefFn, AllocateFn,
                              FreeFn>::template IteratorImpl<Reverse> &
GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::IteratorImpl<Reverse>::
operator++() {
  if constexpr (Reverse) {
    ptr_ = deref(ptr_).prev;
  } else {
    ptr_ = deref(ptr_).next;
  }
  return *this;
}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
template <bool Reverse>
FORCE_INLINE GenericLocalList<NodePtr, DerefFn, AllocateFn,
                              FreeFn>::template IteratorImpl<Reverse>
GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::IteratorImpl<Reverse>::
operator++(int) {
  auto ret = *this;
  operator++();
  return ret;
}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
template <bool Reverse>
FORCE_INLINE GenericLocalList<NodePtr, DerefFn, AllocateFn,
                              FreeFn>::template IteratorImpl<Reverse> &
GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::IteratorImpl<Reverse>::
operator--() {
  if constexpr (Reverse) {
    ptr_ = deref(ptr_).next;
  } else {
    ptr_ = deref(ptr_).prev;
  }
  return *this;
}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
template <bool Reverse>
FORCE_INLINE GenericLocalList<NodePtr, DerefFn, AllocateFn,
                              FreeFn>::template IteratorImpl<Reverse>
GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::IteratorImpl<Reverse>::
operator--(int) {
  auto ret = *this;
  operator--();
  return ret;
}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
template <bool Reverse>
FORCE_INLINE bool
GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::IteratorImpl<Reverse>::
operator==(const IteratorImpl &o) const {
  return this->ptr_ == o.ptr_;
}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
template <bool Reverse>
FORCE_INLINE bool
GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::IteratorImpl<Reverse>::
operator!=(const IteratorImpl &o) const {
  return this->ptr_ != o.ptr_;
}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
template <bool Reverse>
FORCE_INLINE uint8_t *GenericLocalList<NodePtr, DerefFn, AllocateFn,
                                       FreeFn>::IteratorImpl<Reverse>::
operator*() const {
  return reinterpret_cast<uint8_t *>(deref(ptr_).data);
}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
FORCE_INLINE
GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::GenericLocalList() {}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
FORCE_INLINE
GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::GenericLocalList(
    ListData *list_data)
    : list_data_(list_data) {}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
FORCE_INLINE
GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::GenericLocalList(
    ListData *list_data, StateType state)
    : list_data_(list_data), state_(state) {}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
FORCE_INLINE void
GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::init(NodePtr head_ptr,
                                                             NodePtr tail_ptr) {
  list_data_->head_ptr = head_ptr;
  list_data_->tail_ptr = tail_ptr;
  list_data_->head.next = tail_ptr;
  list_data_->tail.prev = head_ptr;
}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
FORCE_INLINE void
GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::set_list_data(
    ListData *list_data) {
  list_data_ = list_data;
}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
FORCE_INLINE GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::Iterator
GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::begin() const {
  return Iterator(list_data_->head.next, state_);
}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
FORCE_INLINE GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::Iterator
GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::end() const {
  return Iterator(list_data_->tail_ptr, state_);
}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
FORCE_INLINE
    GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::ReverseIterator
    GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::rbegin() const {
  return ReverseIterator(list_data_->tail.prev, state_);
}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
FORCE_INLINE
    GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::ReverseIterator
    GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::rend() const {
  return ReverseIterator(list_data_->head_ptr, state_);
}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
template <bool Reverse>
FORCE_INLINE uint8_t *
GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::insert(
    const IteratorImpl<Reverse> &iter) {
  return const_cast<IteratorImpl<Reverse> *>(&iter)->insert();
}

template <typename NodePtr, typename DerefFn, typename AllocateFn,
          typename FreeFn>
template <bool Reverse>
FORCE_INLINE GenericLocalList<NodePtr, DerefFn, AllocateFn,
                              FreeFn>::template IteratorImpl<Reverse>
GenericLocalList<NodePtr, DerefFn, AllocateFn, FreeFn>::erase(
    const IteratorImpl<Reverse> &iter, uint8_t **data_ptr) {
  return const_cast<IteratorImpl<Reverse> *>(&iter)->erase(data_ptr);
}

template <typename T>
template <bool Reverse>
FORCE_INLINE LocalList<T>::template IteratorImpl<Reverse>::IteratorImpl() {}

template <typename T>
template <bool Reverse>
template <bool OReverse>
FORCE_INLINE LocalList<T>::template IteratorImpl<Reverse>::IteratorImpl(
    TGenericLocalList::template IteratorImpl<OReverse> generic_iter)
    : generic_iter_(generic_iter) {}

template <typename T>
template <bool Reverse>
template <bool OReverse>
FORCE_INLINE LocalList<T>::template IteratorImpl<Reverse>::IteratorImpl(
    const IteratorImpl<OReverse> &o)
    : generic_iter_(o.generic_iter_) {}

template <typename T>
template <bool Reverse>
template <bool OReverse>
FORCE_INLINE LocalList<T>::template IteratorImpl<Reverse> &
    LocalList<T>::template IteratorImpl<Reverse>::
    operator=(const IteratorImpl<OReverse> &o) {
  generic_iter_ = o.generic_iter_;
  return *this;
}

template <typename T>
template <bool Reverse>
FORCE_INLINE LocalList<T>::template IteratorImpl<Reverse> &
LocalList<T>::IteratorImpl<Reverse>::operator++() {
  generic_iter_.operator++();
  return *this;
}

template <typename T>
template <bool Reverse>
FORCE_INLINE LocalList<T>::template IteratorImpl<Reverse>
LocalList<T>::IteratorImpl<Reverse>::operator++(int) {
  auto ret = *this;
  generic_iter_.operator++();
  return ret;
}

template <typename T>
template <bool Reverse>
FORCE_INLINE LocalList<T>::template IteratorImpl<Reverse> &
LocalList<T>::IteratorImpl<Reverse>::operator--() {
  generic_iter_.operator--();
  return *this;
}

template <typename T>
template <bool Reverse>
FORCE_INLINE LocalList<T>::template IteratorImpl<Reverse>
LocalList<T>::IteratorImpl<Reverse>::operator--(int) {
  auto ret = *this;
  generic_iter_.operator--();
  return ret;
}

template <typename T>
template <bool Reverse>
FORCE_INLINE bool LocalList<T>::IteratorImpl<Reverse>::
operator==(const IteratorImpl &o) const {
  return this->generic_iter_ == o.generic_iter_;
}

template <typename T>
template <bool Reverse>
FORCE_INLINE bool LocalList<T>::IteratorImpl<Reverse>::
operator!=(const IteratorImpl &o) const {
  return this->generic_iter_ != o.generic_iter_;
}

template <typename T>
template <bool Reverse>
FORCE_INLINE T &LocalList<T>::IteratorImpl<Reverse>::operator*() const {
  return *reinterpret_cast<T *>(*generic_iter_);
}

template <typename T>
template <bool Reverse>
FORCE_INLINE T *LocalList<T>::IteratorImpl<Reverse>::operator->() const {
  return reinterpret_cast<T *>(*generic_iter_);
}

template <typename T> FORCE_INLINE LocalList<T>::~LocalList() {
  while (unlikely(!empty())) {
    pop_back();
  }
}

template <typename T>
FORCE_INLINE LocalList<T>::LocalList()
    : generic_local_list_(&list_data_, &node_pool_) {
  generic_local_list_.init(reinterpret_cast<NodePtr>(&(list_data_.head)),
                           reinterpret_cast<NodePtr>(&(list_data_.tail)));
}

template <typename T>
FORCE_INLINE LocalList<T>::Iterator LocalList<T>::begin() const {
  auto ret = Iterator(generic_local_list_.begin());
  return ret;
}

template <typename T>
FORCE_INLINE LocalList<T>::Iterator LocalList<T>::end() const {
  auto ret = Iterator(generic_local_list_.end());
  return ret;
}

template <typename T>
FORCE_INLINE LocalList<T>::ReverseIterator LocalList<T>::rbegin() const {
  auto ret = ReverseIterator(generic_local_list_.rbegin());
  return ret;
}

template <typename T>
FORCE_INLINE LocalList<T>::ReverseIterator LocalList<T>::rend() const {
  auto ret = ReverseIterator(generic_local_list_.rend());
  return ret;
}

template <typename T> FORCE_INLINE T &LocalList<T>::front() const {
  auto begin_iter = generic_local_list_.begin();
  return *reinterpret_cast<T *>(*begin_iter);
}

template <typename T> FORCE_INLINE T &LocalList<T>::back() const {
  auto rbegin_iter = generic_local_list_.rbegin();
  return *reinterpret_cast<T *>(*rbegin_iter);
}

template <typename T> FORCE_INLINE uint64_t LocalList<T>::size() const {
  return size_;
}

template <typename T> FORCE_INLINE bool LocalList<T>::empty() const {
  return size_ == 0;
}

template <typename T>
FORCE_INLINE void LocalList<T>::push_front(const T &data) {
  auto begin_iter = generic_local_list_.begin();
  auto *data_ptr = generic_local_list_.insert(begin_iter);
  memcpy(data_ptr, &data, sizeof(data));
  size_++;
}

template <typename T> FORCE_INLINE void LocalList<T>::pop_front() {
  auto begin_iter = generic_local_list_.begin();
  uint8_t *data_ptr;
  generic_local_list_.erase(begin_iter, &data_ptr);
  size_--;
}

template <typename T> FORCE_INLINE void LocalList<T>::push_back(const T &data) {
  auto rbegin_iter = generic_local_list_.rbegin();
  // 在rbegin也就是迭代器的最后一个元素
  // 前或后 （由reverse决定）
  // 插入一个新的节点，并返回节点的data部分的首地址
  auto *data_ptr = generic_local_list_.insert(rbegin_iter);
  // 向新节点的data部分拷贝数据，该节点才真正的创立
  memcpy(data_ptr, &data, sizeof(data));
  size_++;
}

template <typename T> FORCE_INLINE void LocalList<T>::pop_back() {
  auto rbegin_iter = generic_local_list_.rbegin();
  uint8_t *data_ptr;
  generic_local_list_.erase(rbegin_iter, &data_ptr);
  size_--;
}

template <typename T>
template <bool Reverse>
FORCE_INLINE void LocalList<T>::insert(const IteratorImpl<Reverse> &iter,
                                       const T &data) {
  auto *data_ptr = generic_local_list_.insert(iter.generic_iter_);
  memcpy(data_ptr, &data, sizeof(data));
  size_++;
}

template <typename T>
template <bool Reverse>
FORCE_INLINE LocalList<T>::template IteratorImpl<Reverse>
LocalList<T>::erase(const IteratorImpl<Reverse> &iter) {
  size_--;
  uint8_t *data_ptr;
  auto ret = IteratorImpl<Reverse>(
      generic_local_list_.erase(iter.generic_iter_, &data_ptr));
  return ret;
}
} // namespace far_memory
