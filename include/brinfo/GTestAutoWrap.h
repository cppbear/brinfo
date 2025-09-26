#pragma once

// Optional: Auto-wrap Google Test assertion macros to emit BrInfo assertion
// begin/end around their evaluation, so invocations inside assertions get
// in_oracle: 1. This header is high-intrusion; enable explicitly with
// BRINFO_AUTO_WRAP_GTEST and ensure it's included before <gtest/gtest.h>, or
// inject via compiler flag: -include brinfo/GTestAutoWrap.h
//
// We intentionally keep OnTestPartResult in the listener as a fallback to
// cover any assertions not captured by these redefinitions.

#if defined(BRINFO_AUTO_WRAP_GTEST)

#include "brinfo/Runtime.h"

// We need gtest API; if not yet included, include it now. If you want to
// strictly control include order, include this header before gtest.
#if __has_include(<gtest/gtest.h>)
#  include <gtest/gtest.h>
#else
#  error "BRINFO_AUTO_WRAP_GTEST set but <gtest/gtest.h> not found."
#endif

namespace BrInfo { namespace Testing {
struct AssertionScopeGuard {
  ~AssertionScopeGuard() noexcept { ::BrInfo::Runtime::AssertionEnd(); }
};
}}

// Helper to wrap any expression-like assertion macro body
#define BRINFO__ASSERTION_WRAP(macro_name, raw_text, body) \
  do {                                                     \
    ::BrInfo::Runtime::AssertionBegin(macro_name, __FILE__, __LINE__, raw_text); \
    ::BrInfo::Testing::AssertionScopeGuard _brinfo_assert_guard_{}; \
    body;                                                  \
  } while(0)

// Common EXPECT_* wrappers
// We do not need to save originals for these because we re-express them
// via predicate forms (EXPECT/ASSERT_PRED_FORMAT*), avoiding recursion.
#undef EXPECT_TRUE
#define EXPECT_TRUE(cond) BRINFO__ASSERTION_WRAP("EXPECT_TRUE", #cond, EXPECT_PRED_FORMAT1(::testing::internal::IsTrue, cond))

#undef EXPECT_FALSE
#define EXPECT_FALSE(cond) BRINFO__ASSERTION_WRAP("EXPECT_FALSE", #cond, EXPECT_PRED_FORMAT1(::testing::internal::IsFalse, cond))

#undef EXPECT_EQ
#define EXPECT_EQ(a,b) BRINFO__ASSERTION_WRAP("EXPECT_EQ", #a ", " #b, EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperEQ, a, b))

#undef EXPECT_NE
#define EXPECT_NE(a,b) BRINFO__ASSERTION_WRAP("EXPECT_NE", #a ", " #b, EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperNE, a, b))

#undef EXPECT_LT
#define EXPECT_LT(a,b) BRINFO__ASSERTION_WRAP("EXPECT_LT", #a ", " #b, EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperLT, a, b))

#undef EXPECT_LE
#define EXPECT_LE(a,b) BRINFO__ASSERTION_WRAP("EXPECT_LE", #a ", " #b, EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperLE, a, b))

#undef EXPECT_GT
#define EXPECT_GT(a,b) BRINFO__ASSERTION_WRAP("EXPECT_GT", #a ", " #b, EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperGT, a, b))

#undef EXPECT_GE
#define EXPECT_GE(a,b) BRINFO__ASSERTION_WRAP("EXPECT_GE", #a ", " #b, EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperGE, a, b))

// ASSERT_* counterparts: early-return is handled by RAII guard
#undef ASSERT_TRUE
#define ASSERT_TRUE(cond) BRINFO__ASSERTION_WRAP("ASSERT_TRUE", #cond, ASSERT_PRED_FORMAT1(::testing::internal::IsTrue, cond))

#undef ASSERT_FALSE
#define ASSERT_FALSE(cond) BRINFO__ASSERTION_WRAP("ASSERT_FALSE", #cond, ASSERT_PRED_FORMAT1(::testing::internal::IsFalse, cond))

#undef ASSERT_EQ
#define ASSERT_EQ(a,b) BRINFO__ASSERTION_WRAP("ASSERT_EQ", #a ", " #b, ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperEQ, a, b))

#undef ASSERT_NE
#define ASSERT_NE(a,b) BRINFO__ASSERTION_WRAP("ASSERT_NE", #a ", " #b, ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperNE, a, b))

#undef ASSERT_LT
#define ASSERT_LT(a,b) BRINFO__ASSERTION_WRAP("ASSERT_LT", #a ", " #b, ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperLT, a, b))

#undef ASSERT_LE
#define ASSERT_LE(a,b) BRINFO__ASSERTION_WRAP("ASSERT_LE", #a ", " #b, ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperLE, a, b))

#undef ASSERT_GT
#define ASSERT_GT(a,b) BRINFO__ASSERTION_WRAP("ASSERT_GT", #a ", " #b, ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperGT, a, b))

#undef ASSERT_GE
#define ASSERT_GE(a,b) BRINFO__ASSERTION_WRAP("ASSERT_GE", #a ", " #b, ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperGE, a, b))

// Save originals for string and floating macros before redefining
// These macros don't have convenient public predicate helpers with stable
// signatures across gtest versions, so we invoke the original macro inside
// our wrapper body. We alias them first to avoid recursive expansion.
#define BRINFO__ORIG_EXPECT_STREQ EXPECT_STREQ
#define BRINFO__ORIG_EXPECT_STRNE EXPECT_STRNE
#define BRINFO__ORIG_EXPECT_STRCASEEQ EXPECT_STRCASEEQ
#define BRINFO__ORIG_EXPECT_STRCASENE EXPECT_STRCASENE
#define BRINFO__ORIG_ASSERT_STREQ ASSERT_STREQ
#define BRINFO__ORIG_ASSERT_STRNE ASSERT_STRNE
#define BRINFO__ORIG_ASSERT_STRCASEEQ ASSERT_STRCASEEQ
#define BRINFO__ORIG_ASSERT_STRCASENE ASSERT_STRCASENE

#define BRINFO__ORIG_EXPECT_FLOAT_EQ EXPECT_FLOAT_EQ
#define BRINFO__ORIG_EXPECT_DOUBLE_EQ EXPECT_DOUBLE_EQ
#define BRINFO__ORIG_EXPECT_NEAR EXPECT_NEAR
#define BRINFO__ORIG_ASSERT_FLOAT_EQ ASSERT_FLOAT_EQ
#define BRINFO__ORIG_ASSERT_DOUBLE_EQ ASSERT_DOUBLE_EQ
#define BRINFO__ORIG_ASSERT_NEAR ASSERT_NEAR

// Redefine C-string comparisons
#undef EXPECT_STREQ
#define EXPECT_STREQ(a,b) BRINFO__ASSERTION_WRAP("EXPECT_STREQ", #a ", " #b, BRINFO__ORIG_EXPECT_STREQ(a,b))
#undef EXPECT_STRNE
#define EXPECT_STRNE(a,b) BRINFO__ASSERTION_WRAP("EXPECT_STRNE", #a ", " #b, BRINFO__ORIG_EXPECT_STRNE(a,b))
#undef EXPECT_STRCASEEQ
#define EXPECT_STRCASEEQ(a,b) BRINFO__ASSERTION_WRAP("EXPECT_STRCASEEQ", #a ", " #b, BRINFO__ORIG_EXPECT_STRCASEEQ(a,b))
#undef EXPECT_STRCASENE
#define EXPECT_STRCASENE(a,b) BRINFO__ASSERTION_WRAP("EXPECT_STRCASENE", #a ", " #b, BRINFO__ORIG_EXPECT_STRCASENE(a,b))

#undef ASSERT_STREQ
#define ASSERT_STREQ(a,b) BRINFO__ASSERTION_WRAP("ASSERT_STREQ", #a ", " #b, BRINFO__ORIG_ASSERT_STREQ(a,b))
#undef ASSERT_STRNE
#define ASSERT_STRNE(a,b) BRINFO__ASSERTION_WRAP("ASSERT_STRNE", #a ", " #b, BRINFO__ORIG_ASSERT_STRNE(a,b))
#undef ASSERT_STRCASEEQ
#define ASSERT_STRCASEEQ(a,b) BRINFO__ASSERTION_WRAP("ASSERT_STRCASEEQ", #a ", " #b, BRINFO__ORIG_ASSERT_STRCASEEQ(a,b))
#undef ASSERT_STRCASENE
#define ASSERT_STRCASENE(a,b) BRINFO__ASSERTION_WRAP("ASSERT_STRCASENE", #a ", " #b, BRINFO__ORIG_ASSERT_STRCASENE(a,b))

// Redefine floating comparisons (preserve original semantics)
#undef EXPECT_FLOAT_EQ
#define EXPECT_FLOAT_EQ(a,b) BRINFO__ASSERTION_WRAP("EXPECT_FLOAT_EQ", #a ", " #b, BRINFO__ORIG_EXPECT_FLOAT_EQ(a,b))
#undef EXPECT_DOUBLE_EQ
#define EXPECT_DOUBLE_EQ(a,b) BRINFO__ASSERTION_WRAP("EXPECT_DOUBLE_EQ", #a ", " #b, BRINFO__ORIG_EXPECT_DOUBLE_EQ(a,b))
#undef EXPECT_NEAR
#define EXPECT_NEAR(a,b,abs_error) BRINFO__ASSERTION_WRAP("EXPECT_NEAR", #a ", " #b ", " #abs_error, BRINFO__ORIG_EXPECT_NEAR(a,b,abs_error))

#undef ASSERT_FLOAT_EQ
#define ASSERT_FLOAT_EQ(a,b) BRINFO__ASSERTION_WRAP("ASSERT_FLOAT_EQ", #a ", " #b, BRINFO__ORIG_ASSERT_FLOAT_EQ(a,b))
#undef ASSERT_DOUBLE_EQ
#define ASSERT_DOUBLE_EQ(a,b) BRINFO__ASSERTION_WRAP("ASSERT_DOUBLE_EQ", #a ", " #b, BRINFO__ORIG_ASSERT_DOUBLE_EQ(a,b))
#undef ASSERT_NEAR
#define ASSERT_NEAR(a,b,abs_error) BRINFO__ASSERTION_WRAP("ASSERT_NEAR", #a ", " #b ", " #abs_error, BRINFO__ORIG_ASSERT_NEAR(a,b,abs_error))

// Notes:
// - We do not auto-wrap EXPECT_THAT/ASSERT_THAT and DEATH tests here to keep
//   compatibility. They can be added later behind separate guards.
// - For stream-based EXPECT_* << messages, the predicate forms still support
//   additional message streaming after the macro invocation.
// - We intentionally do not redefine SUCCEED() and GTEST_SKIP() here to avoid
//   recursion/ordering pitfalls; the listener's OnTestPartResult still logs
//   them post-hoc.

#endif // BRINFO_AUTO_WRAP_GTEST
