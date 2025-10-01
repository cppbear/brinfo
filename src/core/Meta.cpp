#include "brinfo/Meta.h"
#include "brinfo/CondChain.h"
#include "brinfo/Utils.h"
#include "clang/Basic/SourceManager.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace BrInfo {

// static storage definitions
std::vector<ConditionMeta> MetaCollector::Conditions;
std::vector<FunctionMetaEntry> MetaCollector::Functions;
std::vector<ChainMetaEntry> MetaCollector::Chains;
std::unordered_map<std::string, uint32_t> MetaCollector::ConditionKey2Id;
std::unordered_map<uint64_t, uint32_t> MetaCollector::FuncHash2Id;

uint64_t MetaCollector::hashCombine(uint64_t H, uint64_t V) {
  // simple mix
  H ^= V + 0x9e3779b97f4a7c15ULL + (H << 6) + (H >> 2);
  return H;
}

uint64_t
MetaCollector::rollingHash(const std::vector<std::pair<uint32_t, bool>> &Seq) {
  uint64_t H = 1469598103934665603ULL;
  for (auto &P : Seq) {
    uint64_t Mixed = ((uint64_t)P.first << 1) | (uint64_t)(P.second ? 1 : 0);
    H ^= Mixed;
    H *= 1099511628211ULL;
  }
  return H;
}

uint64_t MetaCollector::returnHash(const std::string &Str) {
  if (Str.empty())
    return 0ULL;
  return BrInfo::hash64(Str);
}

uint64_t MetaCollector::conditionHash(const std::string &File, unsigned Line,
                                      const std::string &Cond) {
  std::string Key = File + ":" + std::to_string(Line) + ":" + Cond;
  return BrInfo::hash64(Key);
}

uint32_t MetaCollector::getOrCreateConditionId(const std::string &File,
                                               unsigned Line,
                                               const std::string &CondNorm,
                                               const std::string &Kind) {
  std::string Key = File + "#" + std::to_string(Line) + "#" + CondNorm;
  auto It = ConditionKey2Id.find(Key);
  if (It != ConditionKey2Id.end())
    return It->second;
  uint32_t NewId = (uint32_t)Conditions.size();
  ConditionMeta Meta;
  Meta.Id = NewId;
  Meta.File = File;
  Meta.Line = Line;
  Meta.CondNorm = CondNorm;
  Meta.Kind = Kind;
  Meta.Hash = conditionHash(File, Line, CondNorm);
  Conditions.push_back(Meta);
  ConditionKey2Id[Key] = NewId;
  return NewId;
}

uint32_t MetaCollector::getOrCreateFunctionId(uint64_t FuncHash,
                                              const std::string &Signature,
                                              const std::string &Name,
                                              const std::string &File) {
  auto It = FuncHash2Id.find(FuncHash);
  if (It != FuncHash2Id.end())
    return It->second;
  uint32_t NewId = (uint32_t)Functions.size();
  FunctionMetaEntry F;
  F.FuncId = NewId;
  F.Signature = Signature;
  F.Name = Name;
  F.File = File;
  F.FuncHash = FuncHash;
  Functions.push_back(F);
  FuncHash2Id[FuncHash] = NewId;
  return NewId;
}

std::string MetaCollector::nowISO8601() {
  using namespace std::chrono;
  auto Now = system_clock::now();
  auto TimeT = system_clock::to_time_t(Now);
  std::tm TM;
  gmtime_r(&TimeT, &TM);
  char Buf[32];
  strftime(Buf, sizeof(Buf), "%Y-%m-%dT%H:%M:%SZ", &TM);
  return std::string(Buf);
}

void MetaCollector::recordFunction(
    const clang::FunctionDecl *FD, clang::ASTContext *Context,
    const std::string &Signature,
    const std::vector<class CondChainInfo> &CondChains,
    const std::unordered_set<unsigned> &MinCover,
    const std::vector<std::string> &ReturnStrs) {

  std::string FilePath =
      Context->getSourceManager().getFilename(FD->getLocation()).str();
  uint64_t FuncHash = BrInfo::hash64(Signature);
  uint32_t FuncId = getOrCreateFunctionId(FuncHash, Signature,
                                          FD->getNameAsString(), FilePath);

  // build condition sequences
  for (unsigned I = 0; I < CondChains.size(); ++I) {
    const auto &ChainInfo = CondChains[I];
    if (ChainInfo.IsContra)
      continue;
    ChainMetaEntry Entry;
    std::ostringstream oss;
    oss << std::setw(3) << std::setfill('0') << I;
    Entry.ChainId = oss.str();
    Entry.FuncHash = FuncHash;
    bool Min = MinCover.find(I) != MinCover.end();
    Entry.MinCover = Min;
    // sequence
    for (auto &CS : ChainInfo.Chain) {
      if (!CS.Condition)
        continue;
      // get location line
      unsigned Line = 0;
      const clang::Stmt *S = CS.Condition->getCond();
      if (S) {
        auto &SM = Context->getSourceManager();
        Line = SM.getSpellingLineNumber(S->getBeginLoc());
        FilePath = SM.getFilename(S->getBeginLoc()).str();
      }
      std::string CondNorm = CS.Condition->getCondStr();
      bool Val = CS.Flag;
      if (CS.Condition->isNot())
        Val = !Val;
      // map CondKind enum to string
      std::string KindStr;
      switch (CS.Condition->getKind()) {
      case BaseCond::IF:
        KindStr = "IF";
        break;
      case BaseCond::CASE:
        KindStr = "CASE";
        break;
      case BaseCond::DEFAULT:
        KindStr = "DEFAULT";
        break;
      case BaseCond::LOOP:
        KindStr = "LOOP";
        break;
      case BaseCond::TRY:
        KindStr = "TRY";
        break;
      };
      uint32_t Cid = getOrCreateConditionId(FilePath, Line, CondNorm, KindStr);
      Entry.Sequence.push_back({Cid, Val});
      // record in function meta set
      Functions[FuncId].ConditionIds.insert(Cid);
    }
    Entry.Signature = rollingHash(Entry.Sequence);
    if (I < ReturnStrs.size()) {
      Entry.ReturnHash = returnHash(ReturnStrs[I]);
      if (!ReturnStrs[I].empty()) {
        ReturnExprMeta R{Entry.ChainId, Entry.ReturnHash, ReturnStrs[I]};
        Functions[FuncId].Returns.push_back(R);
      }
    }
    Chains.push_back(std::move(Entry));
  }
}

void MetaCollector::dumpAll(const std::string &ProjectRoot) {
  std::string Version = nowISO8601();
  // conditions
  json JCond;
  JCond["analysis_version"] = Version;
  for (auto &C : Conditions) {
    JCond["conditions"].push_back({{"id", C.Id},
                                   {"file", C.File},
                                   {"line", C.Line},
                                   {"cond_norm", C.CondNorm},
                                   {"kind", C.Kind},
                                   {"hash", toHex64(C.Hash)}});
  }
  // functions
  json JFunc;
  JFunc["analysis_version"] = Version;
  for (auto &F : Functions) {
    json JF;
    JF["func_id"] = F.FuncId;
    JF["signature"] = F.Signature;
    JF["name"] = F.Name;
    JF["file"] = F.File;
    JF["hash"] = toHex64(F.FuncHash);
    for (auto Cid : F.ConditionIds)
      JF["condition_ids"].push_back(Cid);
    for (auto &R : F.Returns) {
      JF["return_exprs"].push_back({{"chain_id", R.ChainId},
                                    {"ret_hash", toHex64(R.ReturnHash)},
                                    {"ret_norm", R.ReturnNorm}});
    }
    JFunc["functions"].push_back(JF);
  }
  // chains
  json JChains;
  JChains["analysis_version"] = Version;
  for (auto &Ch : Chains) {
    json JC;
    JC["chain_id"] = Ch.ChainId;
    JC["func_hash"] = toHex64(Ch.FuncHash);
    JC["mincover"] = Ch.MinCover;
    JC["signature"] = toHex64(Ch.Signature);
    JC["return_hash"] = toHex64(Ch.ReturnHash);
    for (auto &E : Ch.Sequence) {
      JC["sequence"].push_back({{"cond_id", E.first}, {"value", E.second}});
    }
    JChains["chains"].push_back(JC);
  }

  std::filesystem::path OutDir = ProjectRoot + "/llm_reqs";
  std::filesystem::create_directories(OutDir);
  {
    std::ofstream Os((OutDir / "conditions.meta.json").string());
    Os << JCond.dump(2);
  }
  {
    std::ofstream Os((OutDir / "functions.meta.json").string());
    Os << JFunc.dump(2);
  }
  {
    std::ofstream Os((OutDir / "chains.meta.json").string());
    Os << JChains.dump(2);
  }
}

} // namespace BrInfo
