#pragma once

// Google Test integration helpers for BrInfo runtime tracing.
// This header is optional. Include it in your test binary only.

#include "brinfo/Runtime.h"

// Try to include gtest if available to enable the listener and wrappers without
// requiring a strict include order. If gtest isn't available, these helpers
// quietly stay disabled and tests can still include this header.
#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#define BRINFO_HAS_GTEST 1
#endif

namespace BrInfo {
namespace Testing {

// RAII helper used by the BRINFO_CALL macro to ensure EndInvocation is called
// even when exceptions are thrown.
struct InvocationScopeGuard {
  ~InvocationScopeGuard() noexcept { ::BrInfo::Runtime::EndInvocation(); }
};

// Wrap a function call expression with BeginInvocation/EndInvocation.
// Usage:
//   auto v = BRINFO_CALL(myFunc(x));
// Overload without target function hash:
#define BRINFO_CALL(expr)                                                                            \
  ([&](){                                                                                            \
     ::BrInfo::Runtime::BeginInvocation(__FILE__, __LINE__, #expr, 0);                               \
     ::BrInfo::Testing::InvocationScopeGuard _brinfo_scope_guard_{};                                 \
     return (expr);                                                                                  \
   }())

// Variant providing an explicit target func hash (if you have it available),
// e.g., from a precomputed signature hash of the callee.
#define BRINFO_CALL_F(expr, func_hash)                                                               \
  ([&](){                                                                                            \
     ::BrInfo::Runtime::BeginInvocation(__FILE__, __LINE__, #expr, (func_hash));                     \
     ::BrInfo::Testing::InvocationScopeGuard _brinfo_scope_guard_{};                                 \
     return (expr);                                                                                  \
   }())

// Optional assertion wrappers to precisely mark oracle sections.
// Prefer using these when you want invocations inside the assertion to be
// marked as in_oracle. Otherwise, you can skip these and rely on segments
// by line partitioning later.
#define BRINFO_ASSERTION_BEGIN(macro_name, raw_text) \
  ::BrInfo::Runtime::AssertionBegin(macro_name, __FILE__, __LINE__, raw_text)
#define BRINFO_ASSERTION_END() \
  ::BrInfo::Runtime::AssertionEnd()

// Example wrappers (non-intrusive): define new macros instead of overriding gtest's.
#ifdef BRINFO_HAS_GTEST
#define BRINFO_EXPECT_EQ(a,b)                                                                        \
  do {                                                                                               \
    ::BrInfo::Runtime::AssertionBegin("EXPECT_EQ", __FILE__, __LINE__, #a ", " #b);                \
    EXPECT_EQ(a,b);                                                                                  \
    ::BrInfo::Runtime::AssertionEnd();                                                               \
  } while(0)

#define BRINFO_EXPECT_TRUE(x)                                                                        \
  do {                                                                                               \
    ::BrInfo::Runtime::AssertionBegin("EXPECT_TRUE", __FILE__, __LINE__, #x);                       \
    EXPECT_TRUE(x);                                                                                  \
    ::BrInfo::Runtime::AssertionEnd();                                                               \
  } while(0)
#endif

// Google Test listener that emits test_start/test_end events.
// Register in main():
//   ::testing::UnitTest::GetInstance()->listeners().Append(new BrInfo::Testing::GTestListener());
#ifdef BRINFO_HAS_GTEST
class GTestListener : public ::testing::EmptyTestEventListener {
public:
  void OnTestStart(const ::testing::TestInfo& info) override {
    ::BrInfo::Runtime::BeginTest(info.test_suite_name(), info.name(), info.file(), info.line());
  }
  void OnTestEnd(const ::testing::TestInfo& info) override {
    const char* status = info.result()->Passed() ? "PASSED" : "FAILED";
    ::BrInfo::Runtime::EndTest(status);
  }
  void OnTestPartResult(const ::testing::TestPartResult& result) override {
    // Auto-emit an assertion event for gtest assertion reports. Note:
    // Many gtest versions only report failures (and SKIP) here; passing
    // EXPECT_* assertions typically do NOT generate a TestPartResult.
    // SUCCEED() does generate a success. This callback runs after the
    // assertion executes, so it cannot mark in_oracle during evaluation.
    // For precise in_oracle on passing assertions, wrap with
    // BRINFO_ASSERTION_BEGIN/END or BRINFO_EXPECT_*.
    const char* macro = nullptr; // gtest doesn't expose the macro name directly
    // Use type as a coarse macro label
    switch (result.type()) {
      case ::testing::TestPartResult::kSuccess: macro = "GTEST_SUCCESS"; break;
      case ::testing::TestPartResult::kNonFatalFailure: macro = "GTEST_NONFATAL"; break;
      case ::testing::TestPartResult::kFatalFailure: macro = "GTEST_FATAL"; break;
      case ::testing::TestPartResult::kSkip: macro = "GTEST_SKIP"; break;
      default: macro = "GTEST"; break;
    }
    const char* file = result.file_name() ? result.file_name() : "";
    unsigned line = result.line_number() > 0 ? static_cast<unsigned>(result.line_number()) : 0u;
    const char* raw = result.summary();
    ::BrInfo::Runtime::AssertionBegin(macro, file, line, raw);
    ::BrInfo::Runtime::AssertionEnd();
  }
};
#endif

} // namespace Testing
} // namespace BrInfo
