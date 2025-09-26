# Offline Report Extractor (Design)

Goal: produce per-assertion triples <prefix, oracle, cond_chain> from NDJSON logs (and optionally meta files).

## Inputs

- Runtime NDJSON events: test_start/end, assertion or assertion_begin/end, invocation_start/end, cond
- Optional: meta describing conditions and normalization fidelity (file, line, cond_norm/hash)

## Core rules

- Partition by `test_id`
- For each assertion window:
  - Prefix calls: invocations with `in_oracle=0` that occur after the previous assertion (or test_start) and before the current assertion
  - Oracle calls: invocations with `in_oracle=1` between assertion_begin and assertion_end (or heuristically around the single `assertion` event)
- For each invocation, aggregate `cond` events with the same `invocation_id` to form its condition chain (order-preserving; optional de-dupe by `cond_hash`)

## Output

- JSONL where each line is an object with:
  - test: {suite, name, full, file, line}
  - assertion: {assert_id, macro, file, line, raw}
  - prefix: [ {invocation_id, call_file, call_line, call_expr} ... ]
  - oracle_calls: like above, optional
  - cond_chains: {invocation_id -> [ {file, line, cond_norm, cond_hash, cond_kind, val} ... ] }

## Edge cases

- Only single `assertion` events: infer oracle window as the nearest invocations with `call_file/line` equal to assertion site and `in_oracle=1` if present
- Missing `in_oracle`: fall back to time locality around assertion
- Multi-thread: cond without `invocation_id` are ignored or assigned to a separate bucket
- Macro argument wrapping disabled: invocations inside assertions may not appear; still works for prefix-only

## CLI sketch

```
brinfo_report \
  --logs runtime.ndjson[.gz] \
  --meta meta_dir \
  --out triples.jsonl \
  [--dedupe-conds] [--suite REGEX] [--test REGEX] [--since TS] [--until TS]
```
