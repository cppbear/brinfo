# Offline Report Extractor (Design)

[中文](./ReportExtractor.zh.md)

Goal: produce per-assertion triples <prefix, oracle, cond_chain> from NDJSON logs (and optionally match/enrich with static meta).

## Inputs

- Runtime NDJSON events: test_start/end, assertion or assertion_begin/end, invocation_start/end, cond
- Optional: meta files (conditions/chains/functions) describing conditions, static chains, and function signatures.

## Core rules

- Partition by `test_id`
- For each assertion window:
  - Prefix calls: invocations with `in_oracle=0` that occur after the previous assertion (or test_start) and before the current assertion
  - Oracle calls: invocations with `in_oracle=1` between assertion_begin and assertion_end (or heuristically around the single `assertion` event)
- For each invocation, aggregate `cond` events with the same `invocation_id` to form its condition chain (order-preserving). Display can optionally de-dupe by `cond_hash`, but matching uses the un-deduped sequence after loop compression.

## Output

- JSONL where each line is an object with:
  - test: {suite, name, full, file, line}
  - assertion: {assert_id, macro, file, line, raw}
  - prefix: [ {invocation_id, call_file, call_line, call_expr} ... ]
  - oracle_calls: like above, optional
  - cond_chains: {invocation_id -> [ {file, line, cond_norm, cond_hash, cond_kind, val, flip} ... ] }
  - invocations: {invocation_id -> { func_hash, signature?, matched_static?, approx_static? }}

## Loop semantics and edge cases

- Loop compression: for loop-head conditions (cond_kind=LOOP), when raw `val` is True (loop entered), keep the first True and the first-iteration body (recursively compressed), and drop the final exit False; if the loop is not entered (raw `val` False), keep that False.
- Only single `assertion` events: infer oracle window as the nearest invocations with `call_file/line` equal to assertion site and `in_oracle=1` if present
- Missing `in_oracle`: fall back to time locality around assertion
- Multi-thread: cond without `invocation_id` are ignored or assigned to a separate bucket
- Macro argument wrapping disabled: invocations inside assertions may not appear; still works for prefix-only

## Matching rules

- Function anchoring via func_hash from runtime cond events.
- Effective boolean is `val XOR flip` (flip = norm_flip) for the purpose of matching with static truth values.
- Exact match requires equality between the loop-compressed runtime sequence `[(cond_hash, val^flip), ...]` and the static sequence from meta.
- Optional approximate matching (same func_hash only): sid-based prefilter + weighted global alignment over `(sid, val^flip)`; emits `approx_static` when enabled and above threshold.

## CLI sketch

```
brinfo_report \
  --logs runtime.ndjson[.gz] \
  --meta meta_dir \
  --out triples.jsonl \
  [--dedupe-conds] [--approx-match] [--approx-topk K] [--approx-threshold T] \
  [--suite REGEX] [--test REGEX] [--since TS] [--until TS]
```
