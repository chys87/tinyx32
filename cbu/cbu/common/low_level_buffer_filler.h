/*
 * cbu - chys's basic utilities
 * Copyright (c) 2020-2021, chys <admin@CHYS.INFO>
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

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <string_view>
#include <type_traits>

#include "cbu/common/byteorder.h"
#include "cbu/common/concepts.h"
#include "cbu/common/fastdiv.h"
#include "cbu/common/faststr.h"

namespace cbu {
inline namespace cbu_low_level_buffer_filler {

template <Integral T, std::endian Order>
struct FillByEndian {
  T value;

  constexpr FillByEndian(T v) noexcept: value(v) {}

  template <Raw_char_type Ch>
  constexpr Ch* operator()(Ch* p) const noexcept {
    return memdrop_bswap<Order>(p, value);
  }
};

template <Integral T>
using FillLittleEndian = FillByEndian<T, std::endian::little>;
template <Integral T>
using FillBigEndian = FillByEndian<T, std::endian::big>;
template <Integral T>
using FillNativeEndian = FillByEndian<T, std::endian::native>;

struct FillOptions {
  std::uint64_t upper_bound = 0;
  unsigned width = 0;
  char fill = '0';

  template <typename Callback>
  constexpr FillOptions with(Callback cb) noexcept {
    FillOptions res = *this;
    cb(&res);
    return res;
  }

  constexpr FillOptions with_width(unsigned width) noexcept {
    return with([width](FillOptions * fo) constexpr noexcept {
      fo->width = width;
    });
  }

  constexpr FillOptions with_fill(char fill) noexcept {
    return with([fill](FillOptions * fo) constexpr noexcept {
      fo->fill = fill;
    });
  }
};

// FillDec converts an unsigned number to a decimal string.
// The generated code is aggressively unrolled so it can be fairly bloated.
template <std::uint64_t UpperBound, FillOptions Options = FillOptions()>
struct FillDec {
  std::uint64_t value;

  template <Raw_char_type Ch>
  constexpr Ch* operator()(Ch* p) const noexcept {
    if constexpr (Options.width > 0) {
      conv_fixed_digit<UpperBound, Options.width>(value, p);
      return p + Options.width;
    } else {
      Ch* q = conv_flexible_digit(value, p);
      std::reverse(p, q);
      return q;
    }
  }

  template <std::uint64_t UB, unsigned K, Raw_char_type Ch>
  static constexpr void conv_fixed_digit(std::uint64_t v, Ch* p) noexcept {
    if constexpr (Options.fill != '0' && K < Options.width) {
      if (v == 0) {
        for (unsigned k = K; k; --k) p[k - 1] = Options.fill;
        return;
      }
    }

    std::uint64_t quot;
    std::uint32_t rem;
    if constexpr (UB == 0) {
      quot = v / 10;
      rem = v % 10;
    } else {
      quot = cbu::fastdiv<10, UB>(v);
      rem = cbu::fastmod<10, UB>(v);
    }

    if constexpr (K <= 1) {
      if constexpr (K == 1) p[0] = Ch(rem + '0');
      return;
    }

    p[K - 1] = Ch(rem + '0');
    // K - 1 should work (as K == 0 doesn't reach here), but GCC's template
    // instantiater apparently doesn't know this.
    constexpr unsigned NextK = K ? K - 1 : 0;
    constexpr std::uint64_t NextUB =
        UB == 0 ? std::uint64_t(-1) / 10 + 1 : (UB - 1) / 10 + 1;
    conv_fixed_digit<NextUB, NextK>(quot, p);
  }

  template <Raw_char_type Ch>
  static constexpr Ch* conv_flexible_digit(std::uint64_t v, Ch* p) noexcept {
    if constexpr (UpperBound == 0) {
      do {
        *p++ = Ch(v % 10 + '0');
      } while ((v /= 10) != 0);
    } else {
      do {
        *p++ = Ch(cbu::fastmod<10, UpperBound>(v) + '0');
      } while ((v = cbu::fastdiv<10, UpperBound>(v)) != 0);
    }
    return p;
  }
};

template <typename T>
FillDec(T) -> FillDec<
    (sizeof(T) >= 8 ? 0 : std::make_unsigned_t<T>(-1) + std::uint64_t(1))>;

struct FillSkip {
  std::ptrdiff_t diff;
  template <typename Ch>
  constexpr Ch* operator()(Ch* p) const noexcept {
    return p + diff;
  }
};

// Low-level buffer filler
template <Char_type Ch>
class LowLevelBufferFiller {
 public:
  constexpr LowLevelBufferFiller() noexcept : p_(nullptr) {}
  explicit constexpr LowLevelBufferFiller(Ch* p) noexcept : p_(p) {}
  // Copy is safe, but makes no sense, so we disable it.
  LowLevelBufferFiller(const LowLevelBufferFiller&) = delete;
  constexpr LowLevelBufferFiller(LowLevelBufferFiller&& o) noexcept :
    p_(std::exchange(o.p_, nullptr)) {}

  LowLevelBufferFiller& operator=(const LowLevelBufferFiller&) = delete;
  constexpr LowLevelBufferFiller& operator=(LowLevelBufferFiller&& o) noexcept {
    swap(o);
    return *this;
  }

  constexpr void swap(LowLevelBufferFiller& o) noexcept { std::swap(p_, o.p_); }

  constexpr LowLevelBufferFiller& operator<<(Ch c) noexcept {
    *p_++ = c;
    return *this;
  }

  constexpr LowLevelBufferFiller& operator<<(
      std::basic_string_view<Ch> v) noexcept {
    auto size = v.size();
    if (std::is_constant_evaluated()) {
      p_ = std::copy_n(v.data(), size, p_);
    } else {
      p_ = static_cast<Ch*>(std::memcpy(p_, v.data(), size)) + size;
    }
    return *this;
  }

  template <typename T>
  requires requires(T&& v) {
    { std::forward<T>(v)(std::declval<Ch*>()) } -> std::convertible_to<Ch*>;
  }
  constexpr LowLevelBufferFiller& operator<<(T&& v) noexcept {
    p_ = std::forward<T>(v)(p_);
    return *this;
  }

  template <typename Callback>
  requires requires(Callback&& cb, LowLevelBufferFiller& filler) {
    { std::forward<Callback>(cb)(filler) } -> std::convertible_to<Ch*>;
  }
  constexpr LowLevelBufferFiller& operator<<(Callback&& cb) noexcept(
      noexcept(cb(std::declval<LowLevelBufferFiller>()))) {
    p_ = std::forward<Callback>(cb)(*this);
    return *this;
  }

  constexpr Ch* pointer() const noexcept { return p_; }

 private:
  Ch* p_;
};

} // inline namespace cbu_low_level_buffer_filler
} // namespace cbu
