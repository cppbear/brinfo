# BrInfo

[English](./README.md)

BrInfo 是一个基于 C++/Clang 的工具集，用于将静态控制流条件与测试运行时行为进行关联。主要能力包括：

- 静态分析/插桩：对条件（IF/CASE/DEFAULT/LOOP/TRY）进行规范化和哈希
- 运行时：以 NDJSON 事件形式输出 test、assertion、invocation、cond
- Google Test 集成：监听器 + 可选的断言宏自动包装
- AST 重写器：自动为测试体内的函数调用添加 `BRINFO_CALL(...)`

目标：从测试运行中产出按断言聚合的三元组 <测试前缀, 测试预言, 条件链>。

## CLI：静态分析器（brinfo）

详见 README.md，对应的命令行参数、示例与输出位置相同。

## 运行时与 GTest 集成

- 在测试中包含 `brinfo/GTestSupport.h` 以注册监听器，并用 `BRINFO_CALL(...)` 包裹需要跟踪的调用点。
- 如果希望断言宏内部的调用自动标注 `in_oracle`，请定义 `BRINFO_AUTO_WRAP_GTEST` 并在 `<gtest/gtest.h>` 之前包含 `brinfo/GTestAutoWrap.h`。
- 或者，使用 AST 重写器 `brinfo_callwrap` 自动重写测试源码（可选支持宏参数内的调用包装）。

详见 docs/GTestIntegration.zh.md 与 docs/CallWrapTool.zh.md。

## AST 重写器（brinfo_callwrap）

该工具定位 gtest 的 TestBody 内的调用并包裹为 `BRINFO_CALL(...)`。常用参数：
- `--only-tests`（默认开启）：仅限 TestBody
- `--allow` 正则：过滤需要包裹的被调函数（全限定名）
- `--wrap-macro-args`：同时包裹断言宏实参中的调用（要求拼写位置在主文件）
- `--print-structure`、`--structure-all`、`--print-calls`：诊断/大纲

此外，它会在被修改的主文件开头注入以下头（仅注入一次）：

```
#define BRINFO_AUTO_WRAP_GTEST
#include "brinfo/GTestAutoWrap.h"
#include "brinfo/GTestSupport.h"
```

## 构建

参照 README.md 的 Build 小节。

## 文档

- docs/GTestIntegration.zh.md（如何绑定 Google Test 上下文）
- docs/Runtime.zh.md（事件模型、字段语义、线程/上下文）
- docs/Instrumentation.zh.md（条件规范化、cond_kind、hash）
- docs/CallWrapTool.zh.md（AST 重写工具参数与原理）
- docs/ReportExtractor.zh.md（三元组 <prefix, oracle, cond_chain> 的离线提取规则）
