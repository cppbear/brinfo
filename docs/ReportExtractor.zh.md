# 离线报告提取器（设计）

[English](./ReportExtractor.md)

目标：从 NDJSON 运行时日志（及可选静态 meta）生成按断言聚合的三元组 <测试前缀, 测试预言, 条件链>，并可选进行静态链匹配与富化。

## 输入

- 运行时 NDJSON 事件：test_start/end、assertion 或 assertion_begin/end、invocation_start/end、cond
- 可选：meta 文件（conditions/chains/functions），包含条件、静态条件链、函数签名等。

## 核心规则

- 按 `test_id` 分区
- 对每个断言窗口：
  - “测试前缀”：在上一个断言（或 test_start）之后、当前断言之前发生的 `in_oracle=0` 的 invocations
  - “断言内部”：在 assertion_begin..assertion_end 之间的 invocations（或对单条 assertion 事件进行近邻推断）
- 对每个 invocation，按 `invocation_id` 聚合其 `cond` 事件，形成条件链（保序）。展示层可选按 `cond_hash` 去重，但匹配使用“循环压缩”后的未去重序列。

## 输出

- JSONL，每行一个对象：
  - test: {suite, name, full, file, line}
  - assertion: {assert_id, macro, file, line, raw}
  - prefix: [ {invocation_id, call_file, call_line, call_expr} ... ]
  - oracle_calls: 同上（可选）
  - cond_chains: {invocation_id -> [ {file, line, cond_norm, cond_hash, cond_kind, val, flip} ... ] }
  - invocations: {invocation_id -> { func_hash, signature?, matched_static?, approx_static? }}

## 循环语义与边界情况

- 只有单条 `assertion` 事件：可用 `call_file/line` 与时间邻近推断 `in_oracle` 窗口
- 缺少 `in_oracle`：退化为基于时间的近邻匹配
- 循环压缩：对循环头（cond_kind=LOOP），若原始 `val` 为 True（进入循环），保留第一次 True 与“第一轮循环体”（递归压缩），并丢弃最终退出 False；若未进入（原始 `val` 为 False）则保留该 False。
- 多线程：没有 `invocation_id` 的 cond 事件可忽略或归入“未归属”桶
- 未启用宏参数包裹：断言内部调用可能缺失，但前缀路径仍可工作

## 匹配规则

- 函数锚定：从运行时 cond 事件读取 func_hash。
- 有效布尔值为 `val XOR flip`（flip=norm_flip），用于与静态真值对齐。
- 精确匹配：循环压缩后的运行时序列 `[(cond_hash, val^flip), ...]` 必须与静态序列完全一致。
- 可选近似匹配（仅在同一 func_hash 下）：基于 sid 的预筛选 + 对 `(sid, val^flip)` 的加权全局对齐；当启用且超过阈值时输出 `approx_static`。

## 命令行草案

```
brinfo_report \
  --logs runtime.ndjson[.gz] \
  --meta meta_dir \
  --out triples.jsonl \
  [--dedupe-conds] [--approx-match] [--approx-topk K] [--approx-threshold T] \
  [--suite REGEX] [--test REGEX] [--since TS] [--until TS]
```
