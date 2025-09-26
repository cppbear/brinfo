# 离线报告提取器（设计）

[English](./ReportExtractor.md)

目标：从 NDJSON 运行时日志（及可选元数据）生成按断言聚合的三元组 <测试前缀, 测试预言, 条件链>。

## 输入

- 运行时 NDJSON 事件：test_start/end、assertion 或 assertion_begin/end、invocation_start/end、cond
- 可选：条件元数据（文件、行、cond_norm/hash 等）

## 核心规则

- 按 `test_id` 分区
- 对每个断言窗口：
  - “测试前缀”：在上一个断言（或 test_start）之后、当前断言之前发生的 `in_oracle=0` 的 invocations
  - “断言内部”：在 assertion_begin..assertion_end 之间的 invocations（或对单条 assertion 事件进行近邻推断）
- 对每个 invocation，按 `invocation_id` 聚合其 `cond` 事件，形成条件链（保序，可选按 `cond_hash` 去重）

## 输出

- JSONL，每行一个对象：
  - test: {suite, name, full, file, line}
  - assertion: {assert_id, macro, file, line, raw}
  - prefix: [ {invocation_id, call_file, call_line, call_expr} ... ]
  - oracle_calls: 同上（可选）
  - cond_chains: {invocation_id -> [ {file, line, cond_norm, cond_hash, cond_kind, val} ... ] }

## 边界情况

- 只有单条 `assertion` 事件：可用 `call_file/line` 与时间邻近推断 `in_oracle` 窗口
- 缺少 `in_oracle`：退化为基于时间的近邻匹配
- 多线程：没有 `invocation_id` 的 cond 事件可忽略或归入“未归属”桶
- 未启用宏参数包裹：断言内部调用可能缺失，但前缀路径仍可工作

## 命令行草案

```
brinfo_report \
  --logs runtime.ndjson[.gz] \
  --meta meta_dir \
  --out triples.jsonl \
  [--dedupe-conds] [--suite REGEX] [--test REGEX] [--since TS] [--until TS]
```
