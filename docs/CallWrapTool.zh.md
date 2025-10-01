# 调用包装工具（brinfo_callwrap）

[English](./CallWrapTool.md)

该 Clang 重写工具会在 Google Test 的 `TestBody` 中，将匹配到的函数调用包裹为 `BRINFO_CALL(...)`。

## 关键行为

- 通过覆写链识别 `testing::Test::TestBody`（含兜底策略）
- 默认仅处理 TestBody（`--only-tests=true`）
- 幂等：跳过已处于 `BRINFO_CALL(` 的调用
- 可选支持宏实参内的调用（`--wrap-macro-args`，且要求拼写在主文件）
- 对于发生了替换的主文件，会在文件顶部自动注入一次：
  - `#define BRINFO_AUTO_WRAP_GTEST`
  - `#include "brinfo/GTestAutoWrap.h"`
  - `#include "brinfo/GTestSupport.h"`

## 参数

- `--allow <regex>`：仅包裹全限定名匹配的被调函数
- `--only-tests[=true|false]`
- `--main-file-only[=true|false]`
- `--wrap-macro-args`

## 安全与限制

- 忽略系统头文件
- 宏参数包裹要求能获取到合法的文件区间（且在主文件）
- 通过宏名检查与词法回看避免二次包裹

## 实现说明：嵌套调用与宏参数

- 后序包裹（post-order）：先包裹内层调用，再包裹外层调用，避免“先替换外层导致子表达式范围失效”而破坏语法。
- 稳定获取文本：外层包裹优先使用 `Rewriter::getRewrittenText(range)`，从而包含已经被包裹的内层文本；仅在必要时回退到词法器文本。
- 宏参数范围：开启 `--wrap-macro-args` 时，对宏内调用的替换范围使用拼写位置构造的 `[begin, endOfToken)` 字符区间，避免吞掉逗号或括号；且仅当拼写位置在主文件时才进行改写。
- 示例

输入：

```
EXPECT_EQ(handleCommand(parseCommand("start"), true, 1), "start: verbose");
```

输出：

```
EXPECT_EQ(BRINFO_CALL(handleCommand(BRINFO_CALL(parseCommand("start")), true, 1)), "start: verbose");
```

若无法安全构造文件字符区间（如来自头文件展开），该调用点将被跳过。

## 常见用法

- 仅包裹被测命名空间：

```
brinfo_callwrap --allow "^my::sut::" -p <build> <tests/*.cpp>
```

- 同时包裹断言宏参数：

```
brinfo_callwrap --allow "^my::sut::" --wrap-macro-args -p <build> <tests/*.cpp>
```
