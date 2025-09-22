#include "brinfo/Runtime.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

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

static void WriteJson(uint64_t funcHash, const char *file, unsigned line,
                      bool value, const char *condNorm,
                      const uint64_t *condHashOpt, const bool *normFlipOpt) {
  LogFile() << "{\"ts\":\"" << NowISO8601() << "\",";
  LogFile() << "\"type\":\"cond\",";
  LogFile() << "\"func\":\"0x" << std::hex << funcHash << std::dec << "\",";
  LogFile() << "\"file\":\"" << (file ? file : "") << "\",";
  LogFile() << "\"line\":" << line << ",";
  LogFile() << "\"val\":" << (value ? 1 : 0);
  if (condNorm) {
    LogFile() << ",\"cond_norm\":\"" << condNorm << "\"";
  }
  if (condHashOpt) {
    LogFile() << ",\"cond_hash\":\"0x" << std::hex << *condHashOpt << std::dec
              << "\"";
  }
  if (normFlipOpt) {
    LogFile() << ",\"norm_flip\":" << (*normFlipOpt ? 1 : 0);
  }
  LogFile() << "}\n";
}

bool LogCond(uint64_t funcHash, const char *file, unsigned line, bool value,
             const char *condNorm, uint64_t condHash, bool normFlip) {
  std::call_once(InitOnce(), []() { DoInit(nullptr); });
  std::lock_guard<std::mutex> lock(FileMutex());
  WriteJson(funcHash, file, line, value, condNorm, &condHash, &normFlip);
  LogFile().flush();
  return value;
}

} // namespace Runtime
} // namespace BrInfo
