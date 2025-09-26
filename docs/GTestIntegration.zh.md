# BrInfo 的 Google Test 集成

[English](./GTestIntegration.md)

本文说明如何将 Google Test 上下文（测试用例、断言）与运行时条件日志关联，从而把（前缀、预言）映射到静态条件链。

前置：测试二进制需链接 `brinfo` 运行时，并能包含 `brinfo/GTestSupport.h`。

## 1）注册 GTest 监听器

在测试 `main` 中添加监听器，产生 `test_start` / `test_end` 事件。

```c++
#include <gtest/gtest.h>
#include <brinfo/GTestSupport.h>

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::UnitTest::GetInstance()->listeners().Append(new BrInfo::Testing::GTestListener());
  return RUN_ALL_TESTS();
}
```

## 2）包裹被测函数调用

使用 `BRINFO_CALL(expr)` 在调用点周围产生 `invocation_start` / `invocation_end`，调用内的条件日志会自动携带 `test_id` / `invocation_id`。

```c++
#include <brinfo/GTestSupport.h>

TEST(ClassifyTemperatureTest, BasicRanges) {
  EXPECT_EQ(BRINFO_CALL(core::classifyTemperature(-5)), -2);

  int res = BRINFO_CALL(core::classifyTemperature(0));
  EXPECT_EQ(res, -1);
}
```

若已知被测函数哈希，使用 `BRINFO_CALL_F(expr, func_hash)`。

## 3）断言事件：自动 vs 显式

注册监听器后，每次断言（成功/失败）会通过 `OnTestPartResult` 自动产生一条 `assertion` 事件（含文件、行号、概要）。这不需要修改测试源码。

但该自动事件是在断言执行后发出，宏参数内的调用不会自动标注 `in_oracle`。如果需要精确标注断言区间，可用显式包装：

```c++
BRINFO_ASSERTION_BEGIN("EXPECT_EQ", "EXPECT_EQ(core::classifyTemperature(-5), -2)");
EXPECT_EQ(BRINFO_CALL(core::classifyTemperature(-5)), -2);
BRINFO_ASSERTION_END();
```

或使用便捷宏（先发 `assertion` 事件，再调用原始 ASSERT/EXPECT）：

```c++
BRINFO_EXPECT_EQ(BRINFO_CALL(core::classifyTemperature(-5)), -2);
```

## 可选：自动重定义 EXPECT_/ASSERT_ 宏

若希望所有断言宏都自动带上 `in_oracle`，可启用 `brinfo/GTestAutoWrap.h` 提供的重定义：

- 方式：定义 `BRINFO_AUTO_WRAP_GTEST` 并在 `<gtest/gtest.h>` 之前包含该头，或使用编译参数注入：
  -D BRINFO_AUTO_WRAP_GTEST
  -include brinfo/GTestAutoWrap.h
- 覆盖范围：`EXPECT/ASSERT_{TRUE,FALSE,EQ,NE,LT,LE,GT,GE}`（基于谓词形式），使用 RAII 确保 fatal 失败时也能发出 `AssertionEnd()`。
- 未覆盖：`EXPECT_THAT/ASSERT_THAT`、死亡测试、`SUCCEED`、`GTEST_SKIP`（仍由监听器事后记录）。
- 建议仍保留监听器，以便兜底记录未被重定义的断言。

### CMake：针对单个测试目标启用自动包装

```cmake
add_executable(my_tests tests/main.cpp tests/foo_test.cpp)

target_compile_definitions(my_tests PRIVATE BRINFO_AUTO_WRAP_GTEST)
# 在所有 TUs 中强制包含
target_compile_options(my_tests PRIVATE -include brinfo/GTestAutoWrap.h)

target_include_directories(my_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(my_tests PRIVATE gtest gtest_main brinfo_runtime)
```

提示：使用 `brinfo_callwrap` 重写测试源码时，如果在主文件中发生了包裹操作，工具会在该文件最顶部自动注入一次：

```
#define BRINFO_AUTO_WRAP_GTEST
#include "brinfo/GTestAutoWrap.h"
#include "brinfo/GTestSupport.h"
```
用于确保断言宏能被自动包装，并可使用监听器/辅助宏。
