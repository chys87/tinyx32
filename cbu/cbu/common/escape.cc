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

#include "cbu/common/escape.h"

#if __has_include(<x86intrin.h>)
# include <x86intrin.h>
#endif
#include <array>
#include <cstdint>
#include <optional>

#include "cbu/common/bit.h"
#include "cbu/common/encoding.h"
#include "cbu/common/fastarith.h"
#include "cbu/common/faststr.h"

namespace cbu {
inline namespace cbu_escape {
namespace {

inline consteval std::array<char, 32> make_special_escape_map() {
  std::array<char, 32> res{};
  // '\v' is not universally accepted.
  res['\b'] = 'b';
  res['\f'] = 'f';
  res['\n'] = 'n';
  res['\r'] = 'r';
  res['\t'] = 't';
  return res;
}

constexpr std::array<char, 32> SPECIAL_ESCAPE_MAP = make_special_escape_map();

inline constexpr char* escape_char(char* w, std::uint8_t c, EscapeStyle style) {
  if (c < 0x20) {
    if (char special = SPECIAL_ESCAPE_MAP[c]) {
      *w++ = '\\';
      *w++ = special;
    } else {
      if (style == EscapeStyle::JSON) {
        *w++ = '\\';
        *w++ = 'u';
        *w++ = '0';
        *w++ = '0';
      } else {
        *w++ = '\\';
        *w++ = 'x';
      }
      *w++ = "0123456789abcdef"[c >> 4];
      *w++ = "0123456789abcdef"[c & 15];
    }
  } else {
    *w++ = '\\';
    *w++ = c;
  }
  return w;
}

inline constexpr bool needs_escaping(std::uint8_t c,
                                     EscapeStyle style) noexcept {
  return (c < 0x20 || c == '\\' || c == '\"' ||
          (c == '/' && style == EscapeStyle::JSON));
}

constexpr char* escape_string_naive(
    char* w, std::string_view src, EscapeStyle style) {
  for (char c : src) {
    if (needs_escaping(c, style)) {
      w = escape_char(w, c, style);
    } else {
      *w++ = c;
    }
  }
  return w;
}

#ifdef __AVX2__
inline constexpr __v32qu v32qu(std::uint8_t c) noexcept {
  return __v32qu{
    c, c, c, c, c, c, c, c,
    c, c, c, c, c, c, c, c,
    c, c, c, c, c, c, c, c,
    c, c, c, c, c, c, c, c};
}

inline __m256i get_encoding_mask(__m256i chars, EscapeStyle style) noexcept {
  __v32qu vq = __v32qu(chars);
  __v32qu mask = ((vq < v32qu(0x20)) | (vq == v32qu('\\')) |
                  (vq == v32qu('\"')));
  if (style == EscapeStyle::JSON) {
    mask |= (vq == v32qu('/'));
  }
  return __m256i(mask);
}

inline char* encode_by_mask(char* w, const char* s,
                            std::uint32_t bmsk,
                            EscapeStyle style,
                            std::uint32_t from = 0,
                            std::uint32_t until = 32) noexcept {
  bmsk &= (-1u << from);
  s += from;
  if (until < 32) {
    bmsk = bzhi(bmsk, until);
  }
  for (std::uint32_t offset : set_bits(bmsk)) {
    while (from < offset) {
      *w++ = *s++;
      ++from;
    }
    w = escape_char(w, *s++, style);
    ++from;
  }
  while (from < until) {
    *w++ = *s++;
    ++from;
  }
  return w;
}
#endif // __AVX2__

} // namespace

char* escape_string(char* w, std::string_view src, EscapeStyle style)
    noexcept {
#ifdef __AVX2__
  if (src.empty()) {
    return w;
  }

  const char* s = src.data();
  std::size_t n = src.size();
  std::size_t misalign = std::uintptr_t(s) & 31;
  s = (const char*)(std::uintptr_t(s) & -32);
  n += misalign;
  __m256i chars = *(const __m256i*)s;
  __m256i msk = get_encoding_mask(chars, style);
  std::uint32_t bmsk = _mm256_movemask_epi8(msk);
  w = encode_by_mask(w, s, bmsk, style,
                     misalign, std::min<std::uint32_t>(32, n));
  s += 32;
  if (n <= 32) {
    return w;
  }
  n -= 32;

  while (n & -32) {
    __m256i chars = *(const __m256i*)s;
    __m256i msk = get_encoding_mask(chars, style);
    if (_mm256_testz_si256(msk, msk)) {
      *(__m256i_u*)w = chars;
      w += 32;
    } else {
      w = encode_by_mask(w, s, _mm256_movemask_epi8(msk), style);
    }
    s += 32;
    n -= 32;
  }

  if (n) {
    __m256i chars = *(const __m256i*)s;
    __m256i msk = get_encoding_mask(chars, style);
    std::uint32_t bmsk = _mm256_movemask_epi8(msk);
    w = encode_by_mask(w, s, bmsk, style, 0, n);
  }
  return w;
#endif // __AVX2__
  return escape_string_naive(w, src, style);
}

void escape_string_append(std::string* dst,
                          std::string_view src, EscapeStyle style) {
  if (!src.empty()) {
    char* p = extend(
        dst, (style == EscapeStyle::JSON) ? 6 * src.size() : 4 * src.size());
    p = escape_string(p, src, style);
    truncate_unsafe(dst, p - dst->data());
  }
}

std::string escape_string(std::string_view src, EscapeStyle style) {
  std::string res;
  escape_string_append(&res, src, style);
  return res;
}

namespace {

inline constexpr std::tuple<UnescapeStringStatus, char*, const char*> tuplize(
    const UnescapeStringResult& result) noexcept {
  return {result.status, result.dst_ptr, result.src_ptr};
}

std::tuple<char, char*, const char*> copy_until_backslash(
    char* dst, const char* src, const char* end) noexcept {
#ifdef __AVX2__
  while (src + 32 <= end) {
    __v32qu val = __v32qu(*(const __m256i_u*)src);
    __m256i mask = __m256i((val == v32qu('\\')) | (val == v32qu('\"')));
    if (_mm256_testz_si256(mask, mask)) {
      *(__m256i_u*)dst = __m256i(val);
      src += 32;
      dst += 32;
    } else {
      unsigned off = ctz(_mm256_movemask_epi8(mask));
      dst = memdrop(dst, __m256i(val), off);
      src += off;
      return {*src, dst, src};
    }
  }
#endif
  while (src < end && *src != '\\' && *src != '\"') {
    *dst++ = *src++;
  }
  if (src >= end) {
    return {'\0', dst, src};
  } else {
    return {*src, dst, src};
  }
}

inline consteval std::array<char, 128> make_unescape_fast_map() noexcept {
  std::array<char, 128> res{};
  res['\\'] = '\\';
  res['\"'] = '\"';
  res['\''] = '\'';
  res['/'] = '/';  // For JSON
  res['a'] = '\a';
  res['b'] = '\b';
  res['e'] = '\x1f';
  res['f'] = '\f';
  res['n'] = '\n';
  res['r'] = '\r';
  res['t'] = '\t';
  res['v'] = '\v';
  return res;
}

inline constexpr std::array<char, 128> UNESCAPE_FAST_MAP =
    make_unescape_fast_map();

inline constexpr std::optional<unsigned> convert_xdigit(std::uint8_t c)
    noexcept {
  if ((c >= '0') && (c <= '9')) {
    return (c - '0');
  } else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f') {
    return (((c | 0x20) - 'a') + 10);
  } else {
    return std::nullopt;
  }
}

inline constexpr std::optional<unsigned> convert_2xdigit(const char *s)
    noexcept {
  auto a = convert_xdigit(*s++);
  if (!a) return a;
  auto b = convert_xdigit(*s++);
  if (!b) return b;
  return *a * 16 + *b;
}

inline std::optional<unsigned> convert_4xdigit(const char *s) noexcept {
#if defined __SSE4_1__ && defined __BMI2__
  __v16qu v = __v16qu(__v4su{mempick_be<uint32_t>(s), 0, 0, 0});
  __v16qu digits = (v - '0' <= 9);
  __v16qu v_small = v | 0x20;
  __v16qu hex = (v_small - 'a' < 6);
  if ((_mm_movemask_epi8(__m128i(digits | hex)) & 0b1111) != 0b1111)
    return std::nullopt;
  __v16qu res = (digits & (v - '0')) | (hex & (v_small - ('a' - 10)));
  uint32_t t = __v4su(res)[0];
  return _pext_u32(t, 0x0f0f0f0f);
#endif
  auto a = convert_xdigit(*s++);
  if (!a) return a;
  auto b = convert_xdigit(*s++);
  if (!b) return b;
  auto c = convert_xdigit(*s++);
  if (!c) return c;
  auto d = convert_xdigit(*s++);
  if (!d) return d;
  return ((*a * 16 + *b) * 16 + *c) * 16 + *d;
}

inline std::optional<unsigned> convert_8xdigit(const char *s) noexcept {
#if defined __x86_64__ && defined __SSE4_1__ && defined __BMI2__
  __v16qu v = __v16qu(__v2du{mempick_be<uint64_t>(s), 0});
  __v16qu digits = (v - '0' <= 9);
  __v16qu v_small = v | 0x20;
  __v16qu hex = (v_small - 'a' < 6);
  if (uint8_t(_mm_movemask_epi8(__m128i(digits | hex))) != 0xff)
    return std::nullopt;
  __v16qu res = (digits & (v - '0')) | (hex & (v_small - ('a' - 10)));
  uint64_t t = __v2du(res)[0];
  return unsigned(_pext_u64(t, 0x0f0f0f0f'0f0f0f0f));
#endif
  auto a = convert_4xdigit(s);
  auto b = convert_4xdigit(s + 4);
  if (!a || !b) return std::nullopt;
  return (*a << 16) + *b;
}

UnescapeStringResult parse_escape_sequence(
    char* dst, const char* src, const char* end) noexcept {
  const char* start_src = src - 1;
  if (src >= end) return {UnescapeStringStatus::INVALID_ESCAPE, dst, start_src};
  std::uint8_t c = *src++;
  char replacement = 0;
  if (c < 0x80) replacement = UNESCAPE_FAST_MAP[c];
  if (replacement != 0) {
    *dst++ = replacement;
    return {UnescapeStringStatus::OK_QUOTE, dst, src};
  } else if (c == 'x') {
    if (src + 2 > end)
      return {UnescapeStringStatus::INVALID_ESCAPE, dst, start_src};
    auto r = convert_2xdigit(src);
    if (!r)
      return {UnescapeStringStatus::INVALID_ESCAPE, dst, start_src};
    src += 2;
    *dst++ = *r;
    return {UnescapeStringStatus::OK_QUOTE, dst, src};
  } else if (c >= '0' && c <= '7') {
    // Various parsers disagree how to parse octal representations
    // overflowing octet.
    // E.g., "\400" is " 0" in Node.js and is "\0" in Python.
    // We take the Node.js approach since it appears more reasonable.
    unsigned ch = c - '0';
    if (src < end && (*src >= '0' && *src <= '7')) {
      ch = ch * 8 + (*src++ - '0');
      if ((ch < 040) && src < end && (*src >= '0' && *src <= '7')) {
        ch = ch * 8 + (*src++ - '0');
      }
    }
    *dst++ = ch;
    return {UnescapeStringStatus::OK_QUOTE, dst, src};
  } else {
    char32_t ch;
    if (c == 'u') {
      if (src + 4 > end)
        return {UnescapeStringStatus::INVALID_ESCAPE, dst, start_src};
      auto r = convert_4xdigit(src);
      if (!r) return {UnescapeStringStatus::INVALID_ESCAPE, dst, start_src};
      src += 4;

      ch = *r;
      if ((ch >= 0xd800) && (ch <= 0xdbff)) {
        // UTF-16 lead surrogate
        if (src + 6 > end || src[0] != '\\' || src[1] != 'u')
          return {UnescapeStringStatus::HEAD_SURROGATE_WITHOUT_TAIL,
                  dst, start_src};
        r = convert_4xdigit(src + 2);
        if (!r || !(*r >= 0xdc00 && *r <= 0xdfff))
          return {UnescapeStringStatus::HEAD_SURROGATE_WITHOUT_TAIL,
                  dst, start_src};
        src += 6;
        ch = 0x10000 + (ch - 0xd800) * 1024 + (*r - 0xdc00);
      } else if ((ch >= 0xdc00) && (ch <= 0xdfff)) {
        // UTF-16 tail surrogate
        return {UnescapeStringStatus::TAIL_SURROGATE_WITHOUT_HEAD,
                dst, start_src};
      }
    } else if (c == 'U') {
      if (src + 8 > end)
        return {UnescapeStringStatus::INVALID_ESCAPE, dst, start_src};
      auto r = convert_8xdigit(src);
      if (!r) return {UnescapeStringStatus::INVALID_ESCAPE, dst, start_src};
      src += 8;
      ch = *r;
    } else {
      return {UnescapeStringStatus::INVALID_ESCAPE, dst, start_src};
    }

    char* new_dst = char32_to_utf8(dst, ch);
    if (new_dst == nullptr)
      return {UnescapeStringStatus::CODE_POINT_OUT_OF_RANGE, dst, start_src};
    dst = new_dst;
    return {UnescapeStringStatus::OK_QUOTE, dst, src};
  }
}

} // namespace

UnescapeStringResult unescape_string(char* dst, const char* src,
                                     const char* end) noexcept {
  for (;;) {
    char c;
    std::tie(c, dst, src) = copy_until_backslash(dst, src, end);
    if (c == '\0')
      return {UnescapeStringStatus::OK_EOS, dst, src};
    if (c == '\"')
      return {UnescapeStringStatus::OK_QUOTE, dst, src};
    // Now c (a.k.a. *src) must be '\\'
    ++src;
    UnescapeStringStatus status;
    std::tie(status, dst, src) = tuplize(parse_escape_sequence(dst, src, end));
    if (status != UnescapeStringStatus::OK_QUOTE)
      return {status, dst, src};
  }
}

} // inline namespace cbu_escape
} // namespace cbu
