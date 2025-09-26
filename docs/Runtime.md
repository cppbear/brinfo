# Runtime Events

[中文](./Runtime.zh.md)

BrInfo emits NDJSON events to correlate tests, assertions, invocations, and runtime conditions.

## Event types

- test_start, test_end
- assertion (or assertion_begin/assertion_end if enabled)
- invocation_start, invocation_end
- cond

Common fields: `ts`, `test_id`; assertion has `assert_id`; invocation has `invocation_id`.

### assertion

- Captured by GTest listener or by macro auto-wrap helpers.
- Fields: `macro`, `file`, `line`, `raw` (rough source), optional result/status.

### invocation_start/_end

- Emitted by `BRINFO_CALL(...)` wrapper around a call entity.
- Fields:
  - `index`, `segment_id`: per-test sequencing
  - `in_oracle`: 1 if inside an active assertion region, else 0
  - `call_file`, `call_line`, `call_expr`

### cond

- Emitted by instrumented code (`Runtime::LogCond` calls).
- Fields:
  - `func` (stable function hash), `cond_hash`, `cond_norm`, `norm_flip`, `cond_kind`
  - `file`, `line`, `val`
  - Inherits current `test_id` and `invocation_id` from thread-local context

## Threading notes

- Context is thread-local; child threads are not automatically adopted.
- If your code spawns threads in the invocation window, those condition logs may lack `invocation_id` (future work).

## Oracle scoping

- `in_oracle` is computed from an internal assertion scope stack.
- If you rely only on the GTest listener (post-hoc assertions), `in_oracle` is 0 for macro-internal calls; enable macro auto-wrap or use explicit assertion wrappers to tag oracle regions.
