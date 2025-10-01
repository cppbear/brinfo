# BrInfo Report CLI

Extract per-assertion triples <prefix, oracle, cond_chain> from runtime NDJSON logs, and optionally match with static meta to enrich with static chains.

## Quick start

```bash
brinfo_report.py \
  --logs examples/runtime.ndjson \
  --meta examples/branch \
  --out examples/triples.jsonl \
  --dedupe-conds
```

- Input: NDJSON lines with event types `test_start`, `assertion`, `invocation_start/end`, `cond`.
- Output: JSONL, one record per assertion.
- Filtering: Use `--suite` or `--test` with substring matching.

## Record schema

- `test`: `{ suite, name, full, file, line }`
- `assertion`: `{ assert_id, macro, file, line, raw }`
- `prefix`: list of calls before the assertion window (`in_oracle` = 0). Each item: `{ invocation_id, call_file, call_line, call_expr }`
- `oracle_calls`: list of calls inside the assertion (`in_oracle` = 1). Same shape as `prefix`.
- `cond_chains`: object keyed by stringified `invocation_id`. Each value is an array of condition events for that call; each event has:
  - `{ file, line, cond_norm, cond_kind, cond_hash, val, flip }`
  - `flip` mirrors runtime field `norm_flip`; matching uses `(val XOR flip)` to align with static truth values.
- `invocations`: object keyed by stringified `invocation_id`. Value contains:
  - `func_hash` (from runtime cond events)
  - `signature` (from `functions.meta.json`)
  - `matched_static` (if matched): array of `{ source, chain_id, cond_hashes }`
    - `cond_hashes` is the static sequence `[(cond_hash, value), ...]` with `value` taken from `chains.meta.json` `sequence.value`.

Note on `--dedupe-conds`:
- Affects display only: `cond_chains` entries for each invocation will deduplicate by `cond_hash`.
- Matching still uses the full original runtime condition sequence (not deduped).

## Static meta integration

The tool loads meta files from `--meta` directory and checks consistency:

1) `conditions.meta.json`
- Expected shape:
  - `{ analysis_version, conditions: [ { id, hash, cond_norm, kind, file, line, ... }, ... ] }`
- Purpose:
  - Build `id → condition` mapping (to resolve `cond_id` into `cond_hash`/`cond_norm`/`kind`).
  - Also build `hash → condition` mapping for lookup/diagnostics.

2) `chains.meta.json`
- Expected shape:
  - `{ analysis_version, chains: [ { func_hash, sequence: [ { cond_id, value }, ... ], ... }, ... ] }`
- Purpose:
  - Resolve each `cond_id` via `conditions.meta.json` to construct static sequences: `[(cond_hash, value), ...]`.
  - Group by `func_hash` into `static_chains_by_func`.

3) `functions.meta.json`
- Expected shape:
  - `{ analysis_version, functions: [ { hash, name?, signature, ... }, ... ] }`
- Purpose:
  - Build `func_hash → function info` mapping to enrich output with `signature` (and optionally name).

Consistency checks:
- If all three files carry `analysis_version` and they differ, the tool prints a warning (does not abort).

## Matching rules (runtime ↔ static)

- Function anchoring: read `func_hash` from runtime cond events (field `func`). If missing, the invocation cannot be matched.
- Runtime sequence: `rseq = [(cond_hash, val XOR flip), ...]` for that invocation, where `flip = norm_flip`.
- Static sequence: `[(cond_hash, value), ...]` from `chains.meta.json`, resolved via `conditions.meta.json`.
- Exact match: only when `rseq` equals the static sequence (same length/order/pairs). On success, write `invocations[*].matched_static`.

Important: `cond_hash` can be path-sensitive (spelling vs expansion locations, absolute vs relative, realpath, `#line` remapping, etc.). Ensure runtime and static sides use the same hashing policy; otherwise matching may fail. A path-insensitive fallback (e.g., canonicalized `cond_norm + cond_kind`) can be added in future.

## Troubleshooting

- Only functions show up, no `matched_static`:
  - Check `analysis_version` consistency across the three meta files.
  - Ensure `chains.meta.json` has non-empty `sequence` entries.
  - Ensure `conditions.meta.json` resolves ids to valid conditions.
  - Verify `cond_hash` generation policy matches between runtime and meta.
  - Confirm runtime cond events carry `func` field.

- `cond_id` starts at 0:
  - Do not treat 0 as falsy. Use key presence checks rather than truthiness for integer IDs.

- Matching fails after `--dedupe-conds`:
  - Matching is unaffected by dedupe; failures likely due to `cond_hash` mismatch or missing static sequences.

## Example

```bash
brinfo_report.py \
  --logs examples/runtime.ndjson \
  --meta examples/branch \
  --out examples/triples.jsonl
```

If `matched_static` exists, the record shows the source meta file, matched `chain_id`, and the static condition sequence including static truth values.
