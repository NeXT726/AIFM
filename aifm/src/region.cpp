#include "region.hpp"
#include "helpers.hpp"
#include "pointer.hpp"
#include "stats.hpp"

namespace far_memory {

Region::Region(uint32_t idx, bool is_local, bool nt, uint8_t *buf_ptr)
    : region_idx_(idx) {
  if (is_local) {
    buf_ptr_ = buf_ptr;
    memset(buf_ptr, 0, kSize);
    if (nt) {
      set_nt();
    }
  }
}

Region &Region::operator=(Region &&other) {
  __builtin_memcpy(this, &other, sizeof(Region));
  other.invalidate();
  return *this;
}

std::optional<uint64_t> Region::allocate_object(uint16_t object_size) {
  // Allocated object's address must be aligned with sizeof(FarMemPtrMeta).
  object_size = helpers::align_to(object_size, sizeof(FarMemPtrMeta));

  if (!is_invalid()) {
    uint32_t start = first_free_byte_idx_;
    uint32_t end = start + object_size;

    // 如果当前对象的大小超过了Region的剩余空间，进入fail
    if (unlikely(end > kSize)) {
      goto fail;
    }
    update_boundaries(/* force = */ false);
    first_free_byte_idx_ = end;

    uint64_t object_addr;
    if (is_local()) {
      // 对Region的引用数加一
      Region::atomic_inc_ref_cnt(1);
      object_addr = reinterpret_cast<uint64_t>(buf_ptr_) + start;
    } else {
      object_addr = region_idx_ * kSize + start;
    }
    return object_addr;
  fail:
    if (is_local() &&
    // 如果该Region剩余的空间不足以插入Object，但足够插入Object的头
    // 则填满该Region剩余的空间，并把填充用的Object设置为无效
    // 也就是剩余的空间无用了，我们把它用无用数据填满就可以了
        ((first_free_byte_idx_ + Object::kHeaderSize) < Region::kSize)) {
      // Mark the remaining space as empty.
      auto obj =
          Object(reinterpret_cast<uint64_t>(buf_ptr_) + first_free_byte_idx_);
      obj.set_data_len(kSize - first_free_byte_idx_);
      // 将object的ptr_addr的前两字节设置为0xff，即为将该obj置为空
      obj.free();
    }
    update_boundaries(/* force = */ true);
  }
  return std::nullopt;
}

} // namespace far_memory
