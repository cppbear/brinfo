#include "brinfo/CondChain.h"
#include "brinfo/Meta.h"
#include "clang/AST/Decl.h"
#include <unordered_set>

namespace BrInfo {
void metaRecordFunction(const clang::FunctionDecl *FD, clang::ASTContext *Ctx,
                        const std::string &Signature,
                        const std::vector<CondChainInfo> &Chains,
                        const std::unordered_set<unsigned> &MinCover,
                        const std::vector<std::string> &ReturnStrs) {
  MetaCollector::recordFunction(FD, Ctx, Signature, Chains, MinCover,
                                ReturnStrs);
}
} // namespace BrInfo
