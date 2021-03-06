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

#include "strutil.h"
#include <string>
#include <string_view>
#include <gtest/gtest.h>

namespace cbu {

using namespace std::literals;

TEST(StrUtilTest, Scnprintf) {
  char buf[16];

  EXPECT_EQ(0, scnprintf(buf, 0, "%s", "abcdefghijklmnopqrstuvwxyz"));
  EXPECT_EQ(0, scnprintf(buf, 1, "%s", "abcdefghijklmnopqrstuvwxyz"));
  EXPECT_EQ(1, scnprintf(buf, 2, "%s", "abcdefghijklmnopqrstuvwxyz"));
}

TEST(StrUtilTest, Strcnt) {
  EXPECT_EQ(0, strcnt("abcabc", '\0'));
  EXPECT_EQ(2, strcnt("abcabc", 'a'));
  EXPECT_EQ(0, strcnt("abcabc", 'x'));
  EXPECT_EQ(4, strcnt("0123456789abcdef012345678901234567890123456789", '9'));
}

TEST(StrUtilTest, Memcnt) {
  EXPECT_EQ(0, memcnt("abcabc"sv, '\0'));
  EXPECT_EQ(2, memcnt("abcabc"sv, 'a'));
  EXPECT_EQ(0, memcnt("abcabc"sv, 'x'));
  EXPECT_EQ(4, memcnt("0123456789abcdef012345678901234567890123456789"sv, '9'));
}

TEST(StrUtilTest, Reverse) {
  char buf[] = "abcdefghijklmnopqrstuvwxyz";
  EXPECT_EQ(std::end(buf) - 1, reverse(std::begin(buf), std::end(buf) - 1));
  EXPECT_EQ("zyxwvutsrqponmlkjihgfedcba", std::string(buf));
}

TEST(StrUtilTest, StrNumCmp) {
  EXPECT_LT(0, strnumcmp("abcd12a", "abcd9a"));
  EXPECT_EQ(0, strnumcmp("abcd12a", "abcd12a"));
  EXPECT_GT(0, strnumcmp("abcd12a", "abcd23a"));
}

TEST(StrUtilTest, StrCmpLengthFirst) {
  EXPECT_LT(strcmp_length_first("z", "ab"), 0);
  EXPECT_LT(strcmp_length_first("ab", "zz"), 0);
  EXPECT_EQ(strcmp_length_first("ab", "ab"), 0);
  EXPECT_GT(strcmp_length_first("zz", "ab"), 0);
  EXPECT_GT(strcmp_length_first("zzz", "ab"), 0);
  EXPECT_GT(strcmp_length_first("aaa", "zz"), 0);
}

} // namespace cbu
