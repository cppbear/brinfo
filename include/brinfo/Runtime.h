#pragma once

#include <cstdint>

namespace BrInfo {
namespace Runtime {

// Initialize runtime logger. If path is nullptr, uses $BRINFO_TRACE_PATH or
// defaults to "llm_reqs/runtime.ndjson" under current working directory.
void Init(const char *path = nullptr);

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
bool LogCond(uint64_t funcHash, const char *file, unsigned line, bool value,
             const char *condNorm, uint64_t condHash, bool normFlip);

} // namespace Runtime
} // namespace BrInfo
