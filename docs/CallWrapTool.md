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

## Safety and limits

- System headers are ignored
- Macro args wrapping requires spelling in main file and a valid file char range
- Idempotency via macro-name check and lexical lookback prevents double wrapping

## Implementation notes: nested calls and macro arguments

- Post-order wrapping for nesting: the rewriter wraps inner calls first and then the outer call. This avoids corrupting source ranges when the outer expression is replaced before its children.
- Stable text capture: when wrapping the outer call, the tool prefers `Rewriter::getRewrittenText(range)` so the already-wrapped inner text is included; it falls back to original lexer text only if needed.
- Macro argument ranges: when `--wrap-macro-args` is enabled, call ranges inside macros are computed from spelling locations with `[begin, endOfToken)` to avoid swallowing commas or parentheses. Only arguments spelled in the main file are rewritten.
- Example

Input:

```
EXPECT_EQ(handleCommand(parseCommand("start"), true, 1), "start: verbose");
```

Output:

```
EXPECT_EQ(BRINFO_CALL(handleCommand(BRINFO_CALL(parseCommand("start")), true, 1)), "start: verbose");
```

If a safe file character range cannot be constructed (e.g., expansion from headers), the tool skips that site.

## Typical usage

- Wrap only your SUT namespace:

```
brinfo_callwrap --allow "^my::sut::" -p <build> <tests/*.cpp>
```

- Also wrap assertion arguments:

```
brinfo_callwrap --allow "^my::sut::" --wrap-macro-args -p <build> <tests/*.cpp>
```
