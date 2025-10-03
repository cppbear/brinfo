#pragma once

#include "nlohmann/json.hpp"
#include "clang/AST/ASTContext.h"
#include "clang/Analysis/CFG.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace BrInfo {

using json = nlohmann::json;

struct ConditionMeta {
  uint32_t Id = 0;
  std::string File;
  unsigned Line = 0;
  std::string CondNorm;
  std::string Kind;  // textual kind (IF, CASE, DEFAULT, LOOP, TRY, etc.)
  uint64_t Hash = 0; // hash(File + ":" + Line + ":" + CondNorm)
};

struct ChainMetaEntry {
  std::string ChainId;                             // e.g. 000
  uint64_t FuncHash = 0;                           // hash(Signature)
  std::vector<std::pair<uint32_t, bool>> Sequence; // (cond_id,value)
  uint64_t Signature = 0;                          // rolling hash over sequence
  bool MinCover = false;
  uint64_t ReturnHash = 0; // 0 if void / no expr collected
};

struct ReturnExprMeta {
  std::string ChainId;
  uint64_t ReturnHash = 0;
  std::string ReturnNorm; // human readable summary
};

struct FunctionMetaEntry {
  uint32_t FuncId = 0;
  std::string Signature; // canonical signature
  std::string Name;      // simple name
  std::string File;      // declaration file
  uint64_t FuncHash = 0;
  std::unordered_set<uint32_t> ConditionIds; // unique cond ids used
  std::vector<ReturnExprMeta> Returns;       // per chain return forms
};

class MetaCollector {
public:
  static void recordFunction(const clang::FunctionDecl *FD,
                             clang::ASTContext *Context,
                             const std::string &Signature,
                             const std::vector<class CondChainInfo> &CondChains,
                             const std::unordered_set<unsigned> &MinCover,
                             const std::vector<std::string> &ReturnStrs);

  // Dump collected meta into ProjectRoot/llm_reqs/meta/<FileName>/...
  static void dumpAll(const std::string &ProjectRoot,
                      const std::string &FileName);

private:
  static uint64_t hashCombine(uint64_t H, uint64_t V);
  static uint64_t
  rollingHash(const std::vector<std::pair<uint32_t, bool>> &Seq);
  static uint64_t returnHash(const std::string &Str);
  static uint64_t conditionHash(const std::string &File, unsigned Line,
                                const std::string &Cond);

  static uint32_t getOrCreateConditionId(const std::string &File, unsigned Line,
                                         const std::string &CondNorm,
                                         const std::string &Kind);
  static uint32_t getOrCreateFunctionId(uint64_t FuncHash,
                                        const std::string &Signature,
                                        const std::string &Name,
                                        const std::string &File);

  static std::string nowISO8601();

  // storage
  static std::vector<ConditionMeta> Conditions;
  static std::vector<FunctionMetaEntry> Functions;
  static std::vector<ChainMetaEntry> Chains;

  // indices
  static std::unordered_map<std::string, uint32_t>
      ConditionKey2Id; // key=file#line#cond
  static std::unordered_map<uint64_t, uint32_t> FuncHash2Id;
};

} // namespace BrInfo
