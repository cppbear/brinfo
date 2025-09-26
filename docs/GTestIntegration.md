# Google Test Integration for BrInfo Runtime

[中文](./GTestIntegration.zh.md)

This guide shows how to attach Google Test context (test case, assertion) and top-level invocations to the runtime condition logs, so you can map (prefix, oracle) to static condition chains.

Prerequisites: your test binary links with `brinfo` library and can include `brinfo/GTestSupport.h`.

## 1) Register the GTest listener

Add the listener to your test `main` to emit `test_start` / `test_end` events.

```c++
#include <gtest/gtest.h>
#include <brinfo/GTestSupport.h>

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::UnitTest::GetInstance()->listeners().Append(new BrInfo::Testing::GTestListener());
  return RUN_ALL_TESTS();
}
```

## 2) Wrap top-level calls to the function under test

Use `BRINFO_CALL(expr)` to emit `invocation_start` / `invocation_end` around a call site. Condition logs produced inside that call will automatically contain `test_id` and `invocation_id`.

```c++
#include <brinfo/GTestSupport.h>

TEST(ClassifyTemperatureTest, BasicRanges) {
  EXPECT_EQ(BRINFO_CALL(core::classifyTemperature(-5)), -2);

  int res = BRINFO_CALL(core::classifyTemperature(0));
  // other code
  EXPECT_EQ(res, -1);
}
```

If you have a precomputed callee hash, use `BRINFO_CALL_F(expr, func_hash)`.

## 3) Assertion events: automatic vs. explicit

With the listener registered, each Google Test assertion (success or failure) will automatically generate an `assertion` event via `OnTestPartResult`, capturing file, line, and a brief summary. This requires no code changes in your tests.

However, this automatic event is emitted after the assertion executes, so invocations inside the assertion won't be marked `in_oracle` automatically. If you need to mark the oracle section precisely (so `invocation_start`/`end` inside it carry `in_oracle: 1`), use the explicit wrappers below.

To explicitly tag the oracle region so invocations inside assertions are marked `in_oracle`, wrap the assertion with the provided helpers:

```c++
BRINFO_ASSERTION_BEGIN("EXPECT_EQ", "EXPECT_EQ(core::classifyTemperature(-5), -2)");
EXPECT_EQ(BRINFO_CALL(core::classifyTemperature(-5)), -2);
BRINFO_ASSERTION_END();
```

Or use convenience wrappers that emit an `assertion` event and then delegate to the original GTest macros:

```c++
BRINFO_EXPECT_EQ(BRINFO_CALL(core::classifyTemperature(-5)), -2);
```

## Notes

- The ordering of fields in `cond` events remains as implemented in the current Runtime (unchanged by these helpers).
- Child thread context adoption is not implemented yet; logs from child threads won't carry `invocation_id`.
- You can adopt this incrementally: start with the listener + `BRINFO_CALL` only; add assertion wrappers later if you need precise prefix/oracle partitioning.
 - Listener auto-logs assertions post-hoc (no source changes needed); use explicit wrappers only when you need `in_oracle` scoping during assertion evaluation.

### Optional: auto-wrap all EXPECT_/ASSERT_ macros

If you want all assertions to carry `in_oracle` automatically, you can opt-in to macro redefinitions provided by `brinfo/GTestAutoWrap.h`.

- Enable by defining `BRINFO_AUTO_WRAP_GTEST` and including the header before `<gtest/gtest.h>`, or inject it via a compile flag:
  -D BRINFO_AUTO_WRAP_GTEST
  -include brinfo/GTestAutoWrap.h
- Covered: `EXPECT/ASSERT_{TRUE,FALSE,EQ,NE,LT,LE,GT,GE}` using predicate forms, with RAII to ensure `AssertionEnd()` even on fatal failures.
- Not covered (by default): `EXPECT_THAT/ASSERT_THAT`, death tests, `SUCCEED`, `GTEST_SKIP` (these are still logged by the listener post-hoc).
- Keep the listener registered as a fallback for anything not redefined.

#### CMake: enable auto-wrap for a specific test target

The easiest way to guarantee coverage across all translation units of a single test binary is to inject the header via compile options:

```cmake
# Assuming your test exe is named my_tests
add_executable(my_tests tests/main.cpp tests/foo_test.cpp)

# Enable auto-wrap only for this target
target_compile_definitions(my_tests PRIVATE BRINFO_AUTO_WRAP_GTEST)

# Inject the header before any includes in every TU of this target
target_compile_options(my_tests PRIVATE -include brinfo/GTestAutoWrap.h)

# Make sure the compiler can find the header
target_include_directories(my_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link your test as usual (gtest, brinfo_runtime, etc.)
target_link_libraries(my_tests PRIVATE gtest gtest_main brinfo_runtime)
```

Notes:
- If your project uses an out-of-tree include path for brinfo, adjust `target_include_directories` accordingly.
- If you prefer include-order control per file, you can include `brinfo/GTestAutoWrap.h` before `<gtest/gtest.h>` instead of using `-include`.

Tip: If you use the `brinfo_callwrap` rewriter on your test source and it performs any wrapping in a main-file, it will automatically inject once at the very top of that file:

```
#define BRINFO_AUTO_WRAP_GTEST
#include "brinfo/GTestAutoWrap.h"
#include "brinfo/GTestSupport.h"
```
This ensures assertion macros are auto-wrapped and the listener utilities are available.
