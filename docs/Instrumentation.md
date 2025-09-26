# Instrumentation and Condition Normalization

[中文](./Instrumentation.zh.md)

BrInfo instruments conditionals to log normalized predicates consistently at runtime.

## Condition kinds

- IF: includes ternary `?:` conditions and short-circuit sub-conditions
- LOOP: loop conditions
- CASE: `switch` case labels
- DEFAULT: `switch` default
- TRY: exception handling guards

## Normalization

- Each condition is canonicalized to a normalized string `cond_norm` (e.g., `x < 0`, `a == b`), plus `norm_flip` to indicate polarity when applicable.
- A stable hash `cond_hash` is computed (xxHash64 preferred, FNV-1a fallback) and rendered as 16-hex digits.
- The runtime key is effectively (file, line, cond_norm) for human inspection; hash enables robust joins.

## Emission points

- The instrumenter inserts `Runtime::LogCond(...)` at condition sites, passing: function hash, file/line, boolean value, `cond_norm`, `cond_hash`, `norm_flip`, and `cond_kind`.

## Notes

- Keep stringification faithful to source (spacing, identifiers) to align with static analysis.
- Switch cases and logical ops are expanded so every branch contributes a precise condition in the chain.
