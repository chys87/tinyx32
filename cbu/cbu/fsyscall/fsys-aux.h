/*
 * cbu - chys's basic utilities
 * Copyright (c) 2013-2020, chys <admin@CHYS.INFO>
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

#include "fsyscall.h"

#if !defined __cplusplus
# error "fsys-aux supports only C++."
#endif
#if __cplusplus < 201703L
# error "C++17 is required."
#endif

#include <cstddef>
#include <type_traits>
#include <utility>

namespace fsys_aux {
inline namespace fsys_aux_readdir {

struct ReadDirNoSkipDots;
struct ReadDirClose;
template <size_t BufSize> struct ReadDirBufSize;

namespace readdir_detail {

using DirEnt = fsys_linux_dirent64;

template <typename...> struct Opt;
template <> struct Opt<> {
  enum : bool { close = false, skip_dots = true };
  enum : size_t { buf_size = 4096 };
};

template <typename... Args>
struct Opt<ReadDirNoSkipDots, Args...> : Opt<Args...> {
  enum : bool { skip_dots = false};
};
template <typename... Args>
struct Opt<ReadDirClose, Args...> : Opt<Args...> {
  enum : bool { close = true };
};
template <size_t BufSize, typename... Args>
struct Opt<ReadDirBufSize<BufSize>, Args...> : Opt<Args...> {
  enum : size_t { buf_size = BufSize };
};

template <typename Callback>
using RawResultOf = std::decay_t<std::invoke_result_t<Callback, DirEnt *>>;

template <typename Callback>
inline std::enable_if_t<std::is_void_v<RawResultOf<Callback>>, std::nullptr_t>
call(const Callback &cb, DirEnt *ent) {
  cb(ent);
  return nullptr;
}

template <typename Callback>
inline std::enable_if_t<!std::is_void_v<RawResultOf<Callback>>,
                        RawResultOf<Callback>>
call(const Callback &cb, DirEnt *ent) {
  return cb(ent);
}

template <typename Callback>
using ret_t = decltype(call(std::declval<Callback>(),
                            static_cast<DirEnt *>(nullptr)));

template <bool>
struct DeferClose {
  constexpr DeferClose(int) noexcept {}
};
template <> struct DeferClose<true> {
  int fd;
  ~DeferClose() noexcept { fsys_close(fd); }
};

} // namespace readdir_detail


// Inline is important for optimizing the callback, mostly likely a lambda
template <typename... Options, typename Callback>
__attribute__((__always_inline__))
inline auto readdir(int fd, const Callback &callback,
                    const readdir_detail::ret_t<Callback> &default_value = {}) {
  using Opt = readdir_detail::Opt<Options...>;
  using readdir_detail::DirEnt;

  readdir_detail::DeferClose<Opt::close> close_obj {fd};
  alignas(DirEnt) char buf[Opt::buf_size];
  ssize_t l;
  while ((l = fsys_getdents64(fd, buf, sizeof(buf))) > 0) {
    char *ptr = buf;
    char *ptrend = buf + l;
    do {
      DirEnt *ent = reinterpret_cast<DirEnt *>(ptr);
      if (!Opt::skip_dots || !ent->skip_dot()) {
        auto res = readdir_detail::call(callback, ent);
        if (res != default_value)
          return res;
      }
      ptr += ent->d_reclen;
    } while (ptr < ptrend);
  }
  return default_value;
}

} // namespace fsys_aux_readdir
} // namespace fsys_aux
