/*
 * cbu - chys's basic utilities
 * Copyright (c) 2019-2021, chys <admin@CHYS.INFO>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of chys <admin@CHYS.INFO> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY chys <admin@CHYS.INFO> ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL chys <admin@CHYS.INFO> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <memory>
#include <new>

namespace cbu {

// This function only deletes the raw memory, without invoking destructor
template <typename T>
inline constexpr void sized_delete(T* p , std::size_t bytes) noexcept {
#ifdef __cpp_sized_deallocation
  if constexpr (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
    ::operator delete(p, bytes, std::align_val_t(alignof(T)));
  else
    ::operator delete(p, bytes);
#else
  if constexpr (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
    ::operator delete(p, std::align_val_t(alignof(T)));
  else
    ::operator delete(p);
#endif
}

// This function only deletes the raw memory, without invoking destructor
template <typename T>
inline constexpr void sized_array_delete(T* p , std::size_t bytes) noexcept {
#ifdef __cpp_sized_deallocation
  if constexpr (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
    ::operator delete[](p, bytes, std::align_val_t(alignof(T)));
  else
    ::operator delete[](p, bytes);
#else
  if constexpr (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
    ::operator delete[](p, std::align_val_t(alignof(T)));
  else
    ::operator delete[](p);
#endif
}

template <typename T>
struct SizedArrayDeleter {
  std::size_t bytes;
  constexpr void operator()(T* p) const noexcept {
    std::destroy_n(p, bytes / sizeof(T));
    sized_array_delete(p, bytes);
  }
};

namespace cbu_memory_detail {

template <typename T, bool DEFAULT_INIT>
inline T* new_and_init_array(std::size_t n) {
  std::size_t bytes = n * sizeof(T);
  T* p;
  if constexpr (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
    p = static_cast<T*>(::operator new[](bytes, std::align_val_t(alignof(T))));
  else
    p = static_cast<T*>(::operator new[](bytes));
  try {
    if constexpr (DEFAULT_INIT)
      std::uninitialized_default_construct_n(p, n);
    else
      std::uninitialized_value_construct_n(p, n);
  } catch (...) {
    sized_array_delete(p, bytes);
    throw;
  }
  return p;
}

}  // namespace cbu_memory_detail

template <typename T>
requires std::is_unbounded_array_v<T>
using sized_unique_ptr = std::unique_ptr<
    T, SizedArrayDeleter<std::remove_extent_t<T>>>;

template <typename T, typename... Args>
requires (!std::is_array_v<T>)
inline std::unique_ptr<T> make_unique(Args&&... args) {
  return std::make_unique<T>(std::forward<Args>(args)...);
}

template <typename T>
requires std::is_unbounded_array_v<T>
inline sized_unique_ptr<T> make_unique(std::size_t n) {
  using V = std::remove_extent_t<T>;
  return sized_unique_ptr<T>(cbu_memory_detail::new_and_init_array<V, false>(n),
                             SizedArrayDeleter<V>{n * sizeof(V)});
}

template <typename T>
requires (!std::is_array_v<T>)
inline std::unique_ptr<T> make_unique_for_overwrite() {
  // GCC 9 doesn't have std::make_unique_for_overwrite yet.
  // return std::make_unique_for_overwrite<T>();
  return std::unique_ptr<T>(new T);
}

template <typename T>
requires std::is_unbounded_array_v<T>
inline sized_unique_ptr<T> make_unique_for_overwrite(std::size_t n) {
  using V = std::remove_extent_t<T>;
  return sized_unique_ptr<T>(cbu_memory_detail::new_and_init_array<V, true>(n),
                             SizedArrayDeleter<V>{n * sizeof(V)});
}

template <typename T, typename... Args>
requires (!std::is_array_v<T>)
inline std::shared_ptr<T> make_shared(Args&&... args) {
  return std::make_shared<T>(std::forward<Args>(args)...);
}

template <typename T>
requires std::is_unbounded_array_v<T>
inline std::shared_ptr<T> make_shared(std::size_t n) {
  using V = std::remove_extent_t<T>;
  return std::shared_ptr<T>(cbu_memory_detail::new_and_init_array<V, false>(n),
                            SizedArrayDeleter<V>{n * sizeof(V)});
}

template <typename T>
requires (!std::is_array_v<T>)
inline std::shared_ptr<T> make_shared_for_overwrite() {
  // GCC 9 doesn't have std::make_shared_for_overwrite yet.
  // return std::make_shared_for_overwrite<T>();
  return std::shared_ptr<T>(new T);
}

template <typename T>
requires std::is_unbounded_array_v<T>
inline std::shared_ptr<T> make_shared_for_overwrite(std::size_t n) {
  using V = std::remove_extent_t<T>;
  return std::shared_ptr<T>(cbu_memory_detail::new_and_init_array<V, true>(n),
                            SizedArrayDeleter<V>{n * sizeof(V)});
}

// OutlinableArray<T> is like array<T> or unique_ptr<T[]>, but allocates
// either on stack or heap depending on size.  It is somewhat a simplified
// version of absl::InlinedVector.
//
// Usage:
//  OutlinableArrayBuffer<T, N> buffer;
//  OutlinableArray<T> array(&buffer, n);
//  (or, OutlinableArray<T> array(&buffer, n,
//                                OutlinableArray<T>::for_overwrite))
//
// The purpose of not embedding buffer in array is for better compiler
// optimization.
template <typename T> class OutlinableArray;

template <typename T, std::size_t N>
struct OutlinableArrayBuffer {
  alignas(T) char buffer[sizeof(T) * N];
};

template <typename T>
class OutlinableArray {
 public:
  struct ForOverwriteTag {};
  static constexpr ForOverwriteTag for_overwrite{};

  using value_type = T;
  using reference = T&;
  using pointer = T*;
  using iterator = T*;

  template <std::size_t N>
  constexpr OutlinableArray(OutlinableArrayBuffer<T, N>* buffer, std::size_t n)
      : ptr_(new (get_pointer(buffer, n)) T[n]()),
        size_(n),
        allocated_(n <= N ? 0 : n) {}

  template <std::size_t N>
  constexpr OutlinableArray(OutlinableArrayBuffer<T, N>* buffer, std::size_t n,
                            ForOverwriteTag)
      : ptr_(new (get_pointer(buffer, n)) T[n]), allocated_(n <= N ? 0 : n) {}

  OutlinableArray(const OutlinableArray&) = delete;
  OutlinableArray& operator=(const OutlinableArray&) = delete;

  constexpr ~OutlinableArray() noexcept {
    std::destroy_n(ptr_, size_);
    if (allocated_) {
#ifdef __cpp_sized_deallocation
      if constexpr (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
        ::operator delete[](ptr_, size_ * sizeof(T),
                            std::align_val_t(alignof(T)));
      else
        ::operator delete[](ptr_, size_ * sizeof(T));
#else
      if constexpr (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
        ::operator delete[](ptr_, std::align_val_t(alignof(T)));
      else
        ::operator delete[](ptr_);
#endif
    }
  }

  constexpr T* get() const noexcept { return ptr_; }
  constexpr T* data() const noexcept { return ptr_; }
  constexpr std::size_t size() const noexcept { return size_; }
  constexpr T* begin() const noexcept { return ptr_; }
  constexpr T* end() const noexcept { return ptr_ + size_; }
  constexpr T* cbegin() const noexcept { return ptr_; }
  constexpr T* cend() const noexcept { return ptr_ + size_; }
  constexpr T& operator[](std::size_t k) const noexcept { return ptr_[k]; }

 private:
  template <std::size_t N>
  constexpr void* get_pointer(OutlinableArrayBuffer<T, N>* buffer,
                              std::size_t n) {
    if (n <= N)
      return buffer->buffer;
    else if constexpr (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
      return ::operator new[](sizeof(T) * n, std::align_val_t(alignof(T)));
    else
      return ::operator new[](sizeof(T) * n);
  }

 private:
  T* ptr_;
  std::size_t size_;
  bool allocated_;
};

}  // namespace cbu
