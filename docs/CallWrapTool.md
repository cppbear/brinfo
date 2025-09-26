# Call Wrap Tool (brinfo_callwrap)

[中文](./CallWrapTool.zh.md)

This Clang-based rewriter wraps selected call expressions inside Google Test `TestBody` with `BRINFO_CALL(...)`.

## Key behaviors

- Detects `TestBody` by override chain to `testing::Test::TestBody` (with fallbacks)
- Default scope is `--only-tests=true`
- Idempotent: skips calls already under `BRINFO_CALL(`
- Optional macro-argument wrapping via `--wrap-macro-args` with main-file spelling constraint
- Auto-inserts once per modified main file:
  - `#define BRINFO_AUTO_WRAP_GTEST`
  - `#include "brinfo/GTestAutoWrap.h"`
  - `#include "brinfo/GTestSupport.h"`

## Flags

- `--allow <regex>`: only wrap callees whose fully-qualified names match
- `--only-tests[=true|false]`
- `--main-file-only[=true|false]`
- `--wrap-macro-args`
- Diagnostics: `--print-structure`, `--structure-all`, `--print-calls`

## Safety and limits

- System headers are ignored
- Macro args wrapping requires spelling in main file and a valid file char range
- Idempotency via macro-name check and lexical lookback prevents double wrapping

## Typical usage

- Wrap only your SUT namespace:

```
brinfo_callwrap --allow "^my::sut::" -p <build> <tests/*.cpp>
```

- Also wrap assertion arguments:

```
brinfo_callwrap --allow "^my::sut::" --wrap-macro-args -p <build> <tests/*.cpp>
```
