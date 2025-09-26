#pragma once

#include <cstdint>

namespace BrInfo {
namespace Runtime {

// Initialize runtime logger. If path is nullptr, uses $BRINFO_TRACE_PATH or
// defaults to "llm_reqs/runtime.ndjson" under current working directory.
void Init(const char *path = nullptr);

// --- Test lifecycle ---
// Begin a test case context. Typically called from a gtest listener.
void BeginTest(const char *suite, const char *name, const char *file,
               unsigned line);

// End the current test case. status examples: "PASSED","FAILED","ABORTED".
void EndTest(const char *status);

// --- Assertion lifecycle (optional but recommended) ---
// Mark entering an assertion site (e.g., EXPECT_EQ). This enables precise
// prefix/oracle partition by tagging subsequent invocations as in-oracle until
// AssertionEnd is called.
void AssertionBegin(const char *macro, const char *file, unsigned line,
                    const char *rawText);

// Mark leaving the current assertion site.
void AssertionEnd();

// --- Invocation lifecycle ---
// Mark the beginning of a top-level invocation of the target function under
// test. Depth-aware: only the outermost Begin/End pair emits start/end events;
// deeper recursion nests are attributed to the same invocation.
// callExpr can be a short, escaped presentation of the call for diagnostics.
// targetFuncHash is optional (0 means unknown/not supplied).
void BeginInvocation(const char *callFile, unsigned callLine,
                     const char *callExpr, uint64_t targetFuncHash = 0);

// Mark the end of the current top-level invocation.
// status examples: "OK","EXCEPTION","EARLY_EXIT" (nullptr -> treated as "OK").
void EndInvocation(const char *status = nullptr);

// Log a boolean condition evaluation.
// funcHash: hash of function signature if available (0 means unknown)
// file: source file path
// line: source line number
// value: evaluated boolean value
// condNorm: normalized condition string
// condHash: hash(file + ":" + line + ":" + condNorm)
// When normalization flips polarity (e.g., '!=' -> '==', '!X' -> 'X'),
// pass normFlip=true. The 'value' should already reflect the normalized
// condition's evaluation; normFlip is recorded for trace transparency.
// condKind: textual kind (e.g. "IF","CASE","DEFAULT","LOOP","TRY","LOGIC")
bool LogCond(uint64_t funcHash, const char *file, unsigned line, bool value,
             const char *condNorm, uint64_t condHash, bool normFlip,
             const char *condKind);

} // namespace Runtime
} // namespace BrInfo
