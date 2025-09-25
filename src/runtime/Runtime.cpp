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

static void WriteJson(uint64_t funcHash, const char *file, unsigned line,
                      bool value, const char *condNorm,
                      const uint64_t *condHash, const bool *normFlip,
                      const char *condKind) {
  LogFile() << "{\"ts\":\"" << NowISO8601() << "\",";
  LogFile() << "\"type\":\"cond\",";
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
