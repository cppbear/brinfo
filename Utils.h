#pragma once
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace BrInfo {
inline std::string toHex64(uint64_t V) {
  std::ostringstream os;
  os << "0x" << std::hex << std::setw(16) << std::setfill('0') << V;
  return os.str();
}

inline uint64_t fnv1a64(const void *Data, size_t Len) {
  const uint8_t *P = static_cast<const uint8_t *>(Data);
  const uint64_t FNV_OFFSET = 1469598103934665603ULL;
  const uint64_t FNV_PRIME = 1099511628211ULL;
  uint64_t H = FNV_OFFSET;
  for (size_t i = 0; i < Len; ++i) {
    H ^= (uint64_t)P[i];
    H *= FNV_PRIME;
  }
  return H;
}

inline uint64_t fnv1a64(const std::string &S) {
  return fnv1a64(S.data(), S.size());
}
} // namespace BrInfo
