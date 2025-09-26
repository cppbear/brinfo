#include "brinfo/Runtime.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace BrInfo {
namespace Runtime {

namespace {
std::mutex &FileMutex() {
  static std::mutex M;
  return M;
}

std::ofstream &LogFile() {
  static std::ofstream Ofs;
  return Ofs;
}

std::once_flag &InitOnce() {
  static std::once_flag F;
  return F;
}

std::string NowISO8601() {
  using namespace std::chrono;
  auto Now = system_clock::now();
  auto TimeT = system_clock::to_time_t(Now);
  std::tm TM;
  gmtime_r(&TimeT, &TM);
  char Buf[32];
  strftime(Buf, sizeof(Buf), "%Y-%m-%dT%H:%M:%SZ", &TM);
  return std::string(Buf);
}

std::string toHex64(uint64_t V) {
  std::ostringstream os;
  os << "0x" << std::hex << std::setw(16) << std::setfill('0') << V;
  return os.str();
}

} // namespace

static void DoInit(const char *path) {
  std::string p;
  if (path && *path) {
    p = path;
  } else if (const char *env = std::getenv("BRINFO_TRACE_PATH")) {
    p = env;
  } else {
    p = "llm_reqs/runtime.ndjson";
  }
  std::filesystem::path out(p);
  std::filesystem::create_directories(out.parent_path());
  LogFile().open(out, std::ios::out | std::ios::app);
}

void Init(const char *path) {
  std::call_once(InitOnce(), [path]() { DoInit(path); });
}

// ---------------- Context state ----------------
struct TestCtx {
  uint64_t id = 0;
  std::string suite;
  std::string name;
  std::string full;
  std::string file;
  unsigned line = 0;
  uint64_t hash = 0;
  std::chrono::steady_clock::time_point start;
  uint64_t nextAssertId = 0;
  uint64_t nextInvocationIndex = 0;
};

struct InvocationFrame {
  uint64_t id = 0;
  uint64_t index = 0; // within test
  uint64_t testId = 0;
  unsigned depth = 0;
  uint64_t targetFuncHash = 0;
  std::string callFile;
  unsigned callLine = 0;
  std::string callExpr;
  uint64_t segmentId = 0;
  bool inOracle = false;
  std::chrono::steady_clock::time_point start;
};

static std::atomic<uint64_t> &NextTestId() {
  static std::atomic<uint64_t> v{1};
  return v;
}

static std::atomic<uint64_t> &NextInvocationId() {
  static std::atomic<uint64_t> v{1};
  return v;
}

static thread_local std::unique_ptr<TestCtx> TLTest;
static thread_local std::vector<InvocationFrame> TLInvStack;
static thread_local bool TLInAssertion = false;
static thread_local uint64_t TLSegmentId = 0; // increases at each assertion

static uint64_t hash64_local(const std::string &s) {
  // simple FNV-1a 64 for local hashing to avoid depending on Utils here
  const uint64_t fnv_offset = 1469598103934665603ULL;
  const uint64_t fnv_prime = 1099511628211ULL;
  uint64_t h = fnv_offset;
  for (unsigned char c : s) {
    h ^= (uint64_t)c;
    h *= fnv_prime;
  }
  return h;
}

// ---------------- Event writers ----------------
static void WriteKV(const char *k, const std::string &v, bool &first) {
  if (!first)
    LogFile() << ",";
  first = false;
  LogFile() << "\"" << k << "\":\"" << v << "\"";
}
static void WriteKV(const char *k, const char *v, bool &first) {
  WriteKV(k, std::string(v ? v : ""), first);
}
static void WriteKV(const char *k, uint64_t v, bool &first, bool hex = false) {
  if (!first)
    LogFile() << ",";
  first = false;
  LogFile() << "\"" << k << "\":";
  if (hex)
    LogFile() << "\"" << toHex64(v) << "\"";
  else
    LogFile() << v;
}

static void EmitTestStart(const TestCtx &T) {
  LogFile() << "{";
  bool first = true;
  WriteKV("ts", NowISO8601(), first);
  WriteKV("type", "test_start", first);
  WriteKV("test_id", T.id, first);
  WriteKV("suite", T.suite, first);
  WriteKV("name", T.name, first);
  WriteKV("full", T.full, first);
  WriteKV("file", T.file, first);
  WriteKV("line", T.line, first);
  WriteKV("hash", T.hash, first, true);
  LogFile() << "}\n";
}

static void EmitTestEnd(const TestCtx &T, const char *status) {
  LogFile() << "{";
  bool first = true;
  WriteKV("ts", NowISO8601(), first);
  WriteKV("type", "test_end", first);
  WriteKV("test_id", T.id, first);
  WriteKV("status", status ? status : "UNKNOWN", first);
  LogFile() << "}\n";
}

static void EmitAssertion(uint64_t testId, uint64_t assertId, const char *macro,
                          const char *file, unsigned line,
                          const char *rawText) {
  LogFile() << "{";
  bool first = true;
  WriteKV("ts", NowISO8601(), first);
  WriteKV("type", "assertion", first);
  WriteKV("test_id", testId, first);
  WriteKV("assert_id", assertId, first);
  WriteKV("macro", macro ? macro : "", first);
  WriteKV("file", file ? file : "", first);
  WriteKV("line", (uint64_t)line, first);
  if (rawText && *rawText)
    WriteKV("raw", rawText, first);
  LogFile() << "}\n";
}

static void EmitInvocationStart(const InvocationFrame &F) {
  LogFile() << "{";
  bool first = true;
  WriteKV("ts", NowISO8601(), first);
  WriteKV("type", "invocation_start", first);
  WriteKV("test_id", F.testId, first);
  WriteKV("invocation_id", F.id, first);
  WriteKV("index", F.index, first);
  WriteKV("segment_id", F.segmentId, first);
  WriteKV("in_oracle", (uint64_t)(F.inOracle ? 1 : 0), first);
  if (!F.callFile.empty())
    WriteKV("call_file", F.callFile, first);
  if (F.callLine)
    WriteKV("call_line", (uint64_t)F.callLine, first);
  if (!F.callExpr.empty())
    WriteKV("call_expr", F.callExpr, first);
  if (F.targetFuncHash)
    WriteKV("target_func", F.targetFuncHash, first, true);
  LogFile() << "}\n";
}

static void EmitInvocationEnd(const InvocationFrame &F, const char *status,
                              uint64_t durationMs) {
  LogFile() << "{";
  bool first = true;
  WriteKV("ts", NowISO8601(), first);
  WriteKV("type", "invocation_end", first);
  WriteKV("test_id", F.testId, first);
  WriteKV("invocation_id", F.id, first);
  WriteKV("segment_id", F.segmentId, first);
  WriteKV("status", status ? status : "OK", first);
  WriteKV("duration_ms", durationMs, first);
  LogFile() << "}\n";
}

// ---------------- Public APIs ----------------
void BeginTest(const char *suite, const char *name, const char *file,
               unsigned line) {
  std::call_once(InitOnce(), []() { DoInit(nullptr); });
  std::lock_guard<std::mutex> lock(FileMutex());
  TLTest = std::make_unique<TestCtx>();
  TLTest->id = NextTestId()++;
  TLTest->suite = suite ? suite : "";
  TLTest->name = name ? name : "";
  TLTest->full = TLTest->suite + "." + TLTest->name;
  TLTest->file = file ? file : "";
  TLTest->line = line;
  TLTest->hash = hash64_local(TLTest->full);
  TLTest->start = std::chrono::steady_clock::now();
  TLSegmentId = 0;
  TLInAssertion = false;
  EmitTestStart(*TLTest);
  LogFile().flush();
}

void EndTest(const char *status) {
  std::call_once(InitOnce(), []() { DoInit(nullptr); });
  std::lock_guard<std::mutex> lock(FileMutex());
  if (TLTest) {
    EmitTestEnd(*TLTest, status ? status : "UNKNOWN");
    LogFile().flush();
    TLTest.reset();
    TLInvStack.clear();
    TLInAssertion = false;
    TLSegmentId = 0;
  }
}

void AssertionBegin(const char *macro, const char *file, unsigned line,
                    const char *rawText) {
  std::call_once(InitOnce(), []() { DoInit(nullptr); });
  std::lock_guard<std::mutex> lock(FileMutex());
  if (!TLTest)
    return;
  TLInAssertion = true;
  uint64_t assertId = TLTest->nextAssertId++;
  EmitAssertion(TLTest->id, assertId, macro, file, line, rawText);
  LogFile().flush();
}

void AssertionEnd() {
  std::call_once(InitOnce(), []() { DoInit(nullptr); });
  std::lock_guard<std::mutex> lock(FileMutex());
  if (!TLTest)
    return;
  TLInAssertion = false;
  // Move to next segment after an assertion finishes
  ++TLSegmentId;
}

void BeginInvocation(const char *callFile, unsigned callLine,
                     const char *callExpr, uint64_t targetFuncHash) {
  std::call_once(InitOnce(), []() { DoInit(nullptr); });
  std::lock_guard<std::mutex> lock(FileMutex());
  if (!TLTest)
    return; // ignore if no active test
  if (!TLInvStack.empty()) {
    // nested (recursion or inner calls) -> just increase depth
    TLInvStack.back().depth++;
    return;
  }
  InvocationFrame F;
  F.id = NextInvocationId()++;
  F.index = TLTest->nextInvocationIndex++;
  F.testId = TLTest->id;
  F.depth = 1;
  F.targetFuncHash = targetFuncHash;
  F.callFile = callFile ? callFile : "";
  F.callLine = callLine;
  F.callExpr = callExpr ? callExpr : "";
  F.segmentId = TLSegmentId;
  F.inOracle = TLInAssertion;
  F.start = std::chrono::steady_clock::now();
  TLInvStack.push_back(F);
  EmitInvocationStart(F);
  LogFile().flush();
}

void EndInvocation(const char *status) {
  std::call_once(InitOnce(), []() { DoInit(nullptr); });
  std::lock_guard<std::mutex> lock(FileMutex());
  if (TLInvStack.empty())
    return;
  InvocationFrame &F = TLInvStack.back();
  if (F.depth > 1) {
    --F.depth;
    return;
  }
  auto end = std::chrono::steady_clock::now();
  uint64_t dur =
      (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                      F.start)
          .count();
  EmitInvocationEnd(F, status ? status : "OK", dur);
  LogFile().flush();
  TLInvStack.pop_back();
}

// ---------------- Cond writer ----------------
static void WriteJson(uint64_t funcHash, const char *file, unsigned line,
                      bool value, const char *condNorm,
                      const uint64_t *condHash, const bool *normFlip,
                      const char *condKind) {
  LogFile() << "{\"ts\":\"" << NowISO8601() << "\",";
  LogFile() << "\"type\":\"cond\",";
  if (TLTest) {
    LogFile() << "\"test_id\":" << TLTest->id << ",";
  }
  if (!TLInvStack.empty()) {
    LogFile() << "\"invocation_id\":" << TLInvStack.back().id << ",";
  }
  LogFile() << "\"func\":\"" << toHex64(funcHash) << "\",";
  LogFile() << "\"cond_hash\":\"" << toHex64(*condHash) << "\",";
  LogFile() << "\"file\":\"" << (file ? file : "") << "\",";
  LogFile() << "\"line\":" << line << ",";
  LogFile() << "\"cond_norm\":\"" << condNorm << "\",";
  LogFile() << "\"cond_kind\":\"" << condKind << "\",";
  LogFile() << "\"val\":" << (value ? 1 : 0) << ",";
  LogFile() << "\"norm_flip\":" << (*normFlip ? 1 : 0);
  LogFile() << "}\n";
}

bool LogCond(uint64_t funcHash, const char *file, unsigned line, bool value,
             const char *condNorm, uint64_t condHash, bool normFlip,
             const char *condKind) {
  std::call_once(InitOnce(), []() { DoInit(nullptr); });
  std::lock_guard<std::mutex> lock(FileMutex());
  WriteJson(funcHash, file, line, value, condNorm, &condHash, &normFlip,
            condKind);
  LogFile().flush();
  return value;
}

} // namespace Runtime
} // namespace BrInfo
