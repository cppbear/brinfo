# 运行时事件（Runtime Events）

BrInfo 以 NDJSON 事件的形式输出测试、断言、调用与条件，便于离线归并与分析。

## 事件类型

- test_start, test_end
- assertion（或 assertion_begin/assertion_end）
- invocation_start, invocation_end
- cond

常用标识：`test_id`、`assert_id`、`invocation_id`（分别用于三类窗口与关联）。

### assertion

- 由 GTest 监听器（或宏自动包装）产生。
- 字段：`macro`、`file`、`line`、`raw`（近似源码表达），可选 `status`。

### invocation_start/_end

- 由 `BRINFO_CALL(...)` 包裹的调用点产生。
- 字段：
  - `index`、`segment_id`：单测内的顺序标识
  - `in_oracle`：是否处于断言上下文（1/0）
  - `call_file`、`call_line`、`call_expr`

### cond

- 由插桩代码（`Runtime::LogCond`）产生。
- 字段：
  - `func`（函数稳定哈希）、`cond_hash`、`cond_norm`、`norm_flip`、`cond_kind`
  - `file`、`line`、`val`
  - 继承当前线程局部上下文中的 `test_id`、`invocation_id`

## 线程注意事项

- 上下文为线程局部；子线程不会自动继承。
- 若在调用窗口内产生新线程，其条件日志可能没有 `invocation_id`（可在后续版本改进）。

## Oracle 标注

- `in_oracle` 来自内部的断言作用域栈。
- 仅使用 GTest 监听器（事后记录）时，断言宏内的调用通常 `in_oracle=0`；如需精确标注，请启用断言宏自动包装或使用显式断言包装宏。
