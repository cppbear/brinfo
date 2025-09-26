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

## 常见用法

- 仅包裹被测命名空间：

```
brinfo_callwrap --allow "^my::sut::" -p <build> <tests/*.cpp>
```

- 同时包裹断言宏参数：

```
brinfo_callwrap --allow "^my::sut::" --wrap-macro-args -p <build> <tests/*.cpp>
```
