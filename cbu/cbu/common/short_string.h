/*
 * cbu - chys's basic utilities
 * Copyright (c) 2020, chys <admin@CHYS.INFO>
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

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "cbu/common/zstring_view.h"

namespace cbu {
inline namespace cbu_short_string {

template <std::size_t MaxLen>
using strlen_t = std::conditional_t<(MaxLen < 65536),
    std::conditional_t<(MaxLen < 256), std::uint8_t, std::uint16_t>,
    size_t>;

enum struct UninitializedShortString {};
inline constexpr UninitializedShortString UNINITIALIZED_SHORT_STRING{};

// This class is good for storage for very short strings.
template <std::size_t MaxLen>
class short_string {
 public:
  using len_t = strlen_t<MaxLen>;

  explicit short_string(UninitializedShortString) noexcept {}

  constexpr short_string() noexcept : s_{}, l_{0} {}

  template <std::size_t LenP1>
  consteval short_string(const char (&src)[LenP1]) noexcept
      : short_string(std::string_view(src, LenP1 - 1)) {
    // Use a static assertion rather than a concept, to prevent
    // longer strings from matching the following std::string_view overload.
    static_assert(LenP1 <= MaxLen + 1);
  }

  constexpr short_string(std::string_view src) noexcept {
    if (std::is_constant_evaluated()) std::fill_n(s_, MaxLen + 1, 0);
    assign(src);
  }

  constexpr char (&buffer() noexcept)[MaxLen + 1] { return s_; }
  constexpr void set_length(len_t l) { l_ = l; }

  constexpr const char* c_str() const noexcept { return s_; }
  constexpr const char* data() const noexcept { return s_; }
  constexpr char operator[](std::size_t n) const noexcept { return s_[n]; }
  constexpr len_t length() const noexcept { return l_; }
  constexpr len_t size() const noexcept { return l_; }
  constexpr const char* begin() const noexcept { return s_; }
  constexpr const char* end() const noexcept { return s_ + l_; }
  constexpr const char* cbegin() const noexcept { return s_; }
  constexpr const char* cend() const noexcept { return s_ + l_; }
  constexpr operator zstring_view() const noexcept { return {s_, l_}; }

  constexpr zstring_view string_view() const noexcept { return {s_, l_}; }
  std::string string() const { return std::string(s_, l_); }

  static constexpr len_t capacity() noexcept { return MaxLen; }

  constexpr void assign(std::string_view rhs) noexcept {
    auto copy_end = std::copy_n(rhs.data(), rhs.length(), s_);
    *copy_end = '\0';
    l_ = rhs.length();
  }

  // For some third-party type traits
  static constexpr inline bool pass_by_ref(short_string*) { return true; }

 private:
  char s_[MaxLen + 1];
  len_t l_;
};

template <std::size_t N> requires (N > 0)
short_string(const char (&)[N]) -> short_string<N - 1>;

// This class is like short_string, but the length is fixed
template <std::size_t Len, bool HasTerminator = false>
class fixed_length_string {
 public:
  explicit fixed_length_string(UninitializedShortString) noexcept {}

  constexpr fixed_length_string() noexcept : s_{} {}
  consteval fixed_length_string(std::string_view src) noexcept : s_{} {
    assign(src);
  }

  constexpr char* data() noexcept { return s_; }
  constexpr const char* data() const noexcept { return s_; }
  constexpr const char* c_str() const noexcept requires HasTerminator {
    return s_;
  }
  static constexpr size_t length() noexcept { return Len; }
  static constexpr size_t size() noexcept { return Len; }
  constexpr char* begin() noexcept { return s_; }
  constexpr char* end() noexcept { return s_ + Len; }
  constexpr const char* begin() const noexcept { return s_; }
  constexpr const char* end() const noexcept { return s_ + Len; }
  constexpr const char* cbegin() const noexcept { return s_; }
  constexpr const char* cend() const noexcept { return s_ + Len; }
  constexpr operator std::string_view() const noexcept { return {s_, Len}; }
  constexpr operator zstring_view() const noexcept requires HasTerminator {
    return {s_, Len};
  }

  constexpr void assign(std::string_view src) noexcept {
    constexpr std::size_t copy_len = std::min(Len, src.size());
    std::copy_n(src.data(), copy_len, s_);
    std::fill_n(s_ + copy_len, sizeof(s_) - copy_len, 0);
  }

  // For some third-party type traits
  static constexpr inline bool pass_by_ref(fixed_length_string*) {
    return true;
  }

 private:
  char s_[Len + HasTerminator];
};

template <std::size_t N>
fixed_length_string(const char (&)[N]) -> fixed_length_string<N - 1>;

} // namespace cbu_short_string
} // namespace cbu
