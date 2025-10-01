# BrInfo Report CLI

Extract per-assertion triples <prefix, oracle, cond_chain> from runtime NDJSON logs.

## Usage

```
brinfo_report.py --logs examples/runtime.ndjson --out triples.jsonl --dedupe-conds
```

- Input: NDJSON lines with types test_start, assertion, invocation_start/end, cond.
- Output: JSONL, one record per assertion.

Notes:
- Uses `in_oracle` to separate prefix vs assertion-internal invocations.
- If your log contains multiple tests, all are processed; use `--suite` or `--test` to filter.
- Meta files are optional; current logic does not require them.
