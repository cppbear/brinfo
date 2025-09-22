#include "brinfo/Utils.h"
#include <clang/AST/AST.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <string>
#include <vector>

using namespace clang;
using namespace clang::tooling;
using namespace llvm;

static cl::OptionCategory InstrCat("brinfo-instrument options");
static cl::opt<std::string> OutputPath("o", cl::desc("Output file path"),
                                       cl::value_desc("file"),
                                       cl::cat(InstrCat));

class IfInstrumentVisitor : public RecursiveASTVisitor<IfInstrumentVisitor> {
  Rewriter &R;
  SourceManager &SM;
  const LangOptions &LO;
  uint64_t CurrentFuncHash = 0;
  int CondDepth = 0;
  struct SwitchCtx {
    std::string File;
    unsigned Line = 0;
    std::string SwitchNorm;
    std::vector<std::string> CaseNorms;
  };
  std::vector<SwitchCtx> SwitchStack;

  static bool containsLogicalOp(const Expr *E) {
    if (!E)
      return false;
    E = E->IgnoreParenImpCasts();
    if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
      if (BO->isLogicalOp())
        return true;
      return containsLogicalOp(BO->getLHS()) || containsLogicalOp(BO->getRHS());
    }
    // Also walk conditional operator children defensively
    if (const auto *CO = dyn_cast<ConditionalOperator>(E)) {
      return containsLogicalOp(CO->getCond()) ||
             containsLogicalOp(CO->getTrueExpr()) ||
             containsLogicalOp(CO->getFalseExpr());
    }
    return false;
  }

  static std::string rtrimSemiSpace(std::string S) {
    while (!S.empty() &&
           (isspace(static_cast<unsigned char>(S.back())) || S.back() == ';'))
      S.pop_back();
    return S;
  }

  static std::string escapeForCxxString(const std::string &S) {
    std::string Out;
    Out.reserve(S.size() + 8);
    for (char c : S) {
      switch (c) {
      case '\\':
        Out += "\\\\";
        break;
      case '"':
        Out += "\\\"";
        break;
      case '\n':
        Out += "\\n";
        break;
      case '\t':
        Out += "\\t";
        break;
      default:
        if ((unsigned char)c < 0x20) {
          // drop other control chars
        } else {
          Out += c;
        }
      }
    }
    return Out;
  }

  // Strict normalization mirroring BaseCond::setCondStr: != -> ==, !X -> X
  std::string condNormFromExpr(const Expr *E) {
    if (!E)
      return "";
    const Expr *PE = E->IgnoreParenImpCasts();
    std::string Norm;
    llvm::raw_string_ostream OS(Norm);
    if (const auto *BO = dyn_cast<BinaryOperator>(PE)) {
      if (BO->getOpcode() == BinaryOperatorKind::BO_NE) {
        BO->getLHS()->IgnoreParenImpCasts()->printPretty(OS, nullptr, LO);
        OS << " == ";
        BO->getRHS()->IgnoreParenImpCasts()->printPretty(OS, nullptr, LO);
        OS.flush();
        return rtrimSemiSpace(Norm);
      }
    } else if (const auto *UO = dyn_cast<UnaryOperator>(PE)) {
      if (UO->getOpcode() == UnaryOperatorKind::UO_LNot) {
        UO->getSubExpr()->IgnoreParenImpCasts()->printPretty(OS, nullptr, LO);
        OS.flush();
        return rtrimSemiSpace(Norm);
      }
    }
    PE->printPretty(OS, nullptr, LO);
    OS.flush();
    return rtrimSemiSpace(Norm);
  }

  // Determine if normalization flipped the logical polarity
  bool condNormFlipped(const Expr *E) {
    if (!E)
      return false;
    const Expr *PE = E->IgnoreParenImpCasts();
    if (const auto *BO = dyn_cast<BinaryOperator>(PE)) {
      return BO->getOpcode() == BinaryOperatorKind::BO_NE;
    }
    if (const auto *UO = dyn_cast<UnaryOperator>(PE)) {
      return UO->getOpcode() == UnaryOperatorKind::UO_LNot;
    }
    return false;
  }

  std::pair<std::string, uint64_t>
  makeCondNormAndHash(const std::string &File, unsigned Line, const Expr *E) {
    std::string Norm = condNormFromExpr(E);
    uint64_t H = BrInfo::hash64(File + ":" + std::to_string(Line) + ":" + Norm);
    return {Norm, H};
  }

public:
  IfInstrumentVisitor(Rewriter &R)
      : R(R), SM(R.getSourceMgr()), LO(R.getLangOpts()) {}

  static std::string buildSignature(const FunctionDecl *FD) {
    const FunctionDecl *CanonicalDecl = FD->getCanonicalDecl();
    std::string Sig = CanonicalDecl->getReturnType().getAsString() + " ";
    if (CanonicalDecl->isCXXClassMember()) {
      Sig += cast<CXXRecordDecl>(CanonicalDecl->getParent())->getNameAsString();
      Sig += "::";
    }
    Sig += CanonicalDecl->getNameAsString();
    Sig += "(";
    int I = 0;
    for (ParmVarDecl *Param : CanonicalDecl->parameters()) {
      if (I++ > 0)
        Sig += ", ";
      Sig += Param->getType().getAsString();
    }
    Sig += ")";
    return Sig;
  }

  bool TraverseFunctionDecl(FunctionDecl *FD) {
    uint64_t Prev = CurrentFuncHash;
    if (FD && FD->doesThisDeclarationHaveABody()) {
      CurrentFuncHash = BrInfo::hash64(buildSignature(FD));
    }
    bool Res = RecursiveASTVisitor::TraverseFunctionDecl(FD);
    CurrentFuncHash = Prev;
    return Res;
  }

  bool VisitIfStmt(IfStmt *If) {
    auto Cond = If->getCond();
    if (!Cond)
      return true;
    // If condition contains logical ops, skip whole-cond wrap; operands will be
    // wrapped.
    if (containsLogicalOp(Cond))
      return true;
    SourceLocation SL = Cond->getBeginLoc();
    if (SL.isInvalid() || !SM.isWrittenInMainFile(SL))
      return true;
    unsigned Line = SM.getSpellingLineNumber(SL);
    std::string File = SM.getFilename(SL).str();
    auto [Norm, H] = makeCondNormAndHash(File, Line, Cond);
    bool Flip = condNormFlipped(Cond);
    std::string Inject = std::string("BrInfo::Runtime::LogCond(") +
                         BrInfo::toHex64(CurrentFuncHash) + ", \"" + File +
                         "\", " + std::to_string(Line) + ", (bool)(";
    R.InsertText(Cond->getBeginLoc(), Inject, true, true);
    std::string Suffix = std::string(") , \"") + escapeForCxxString(Norm) +
                         "\", " + BrInfo::toHex64(H) + ", " +
                         (Flip ? "true" : "false") + ")";
    R.InsertTextAfterToken(Cond->getEndLoc(), Suffix);
    return true;
  }

  bool VisitWhileStmt(WhileStmt *WS) {
    Expr *Cond = WS->getCond();
    if (!Cond)
      return true;
    if (containsLogicalOp(Cond))
      return true;
    SourceLocation SL = Cond->getBeginLoc();
    if (SL.isInvalid() || !SM.isWrittenInMainFile(SL))
      return true;
    unsigned Line = SM.getSpellingLineNumber(SL);
    std::string File = SM.getFilename(SL).str();
    auto [Norm, H] = makeCondNormAndHash(File, Line, Cond);
    bool Flip = condNormFlipped(Cond);
    std::string Inject = std::string("BrInfo::Runtime::LogCond(") +
                         BrInfo::toHex64(CurrentFuncHash) + ", \"" + File +
                         "\", " + std::to_string(Line) + ", (bool)(";
    R.InsertText(Cond->getBeginLoc(), Inject, true, true);
    std::string Suffix = std::string(") , \"") + escapeForCxxString(Norm) +
                         "\", " + BrInfo::toHex64(H) + ", " +
                         (Flip ? "true" : "false") + ")";
    R.InsertTextAfterToken(Cond->getEndLoc(), Suffix);
    return true;
  }

  bool VisitForStmt(ForStmt *FS) {
    Expr *Cond = FS->getCond();
    if (!Cond)
      return true; // for(;;) has no condition
    if (containsLogicalOp(Cond))
      return true;
    SourceLocation SL = Cond->getBeginLoc();
    if (SL.isInvalid() || !SM.isWrittenInMainFile(SL))
      return true;
    unsigned Line = SM.getSpellingLineNumber(SL);
    std::string File = SM.getFilename(SL).str();
    auto [Norm, H] = makeCondNormAndHash(File, Line, Cond);
    bool Flip = condNormFlipped(Cond);
    std::string Inject = std::string("BrInfo::Runtime::LogCond(") +
                         BrInfo::toHex64(CurrentFuncHash) + ", \"" + File +
                         "\", " + std::to_string(Line) + ", (bool)(";
    R.InsertText(Cond->getBeginLoc(), Inject, true, true);
    std::string Suffix = std::string(") , \"") + escapeForCxxString(Norm) +
                         "\", " + BrInfo::toHex64(H) + ", " +
                         (Flip ? "true" : "false") + ")";
    R.InsertTextAfterToken(Cond->getEndLoc(), Suffix);
    return true;
  }

  bool VisitDoStmt(DoStmt *DS) {
    Expr *Cond = DS->getCond();
    if (!Cond)
      return true;
    if (containsLogicalOp(Cond))
      return true;
    SourceLocation SL = Cond->getBeginLoc();
    if (SL.isInvalid() || !SM.isWrittenInMainFile(SL))
      return true;
    unsigned Line = SM.getSpellingLineNumber(SL);
    std::string File = SM.getFilename(SL).str();
    auto [Norm, H] = makeCondNormAndHash(File, Line, Cond);
    bool Flip = condNormFlipped(Cond);
    std::string Inject = std::string("BrInfo::Runtime::LogCond(") +
                         BrInfo::toHex64(CurrentFuncHash) + ", \"" + File +
                         "\", " + std::to_string(Line) + ", (bool)(";
    R.InsertText(Cond->getBeginLoc(), Inject, true, true);
    std::string Suffix = std::string(") , \"") + escapeForCxxString(Norm) +
                         "\", " + BrInfo::toHex64(H) + ", " +
                         (Flip ? "true" : "false") + ")";
    R.InsertTextAfterToken(Cond->getEndLoc(), Suffix);
    return true;
  }

  bool VisitConditionalOperator(ConditionalOperator *CO) {
    Expr *Cond = CO->getCond();
    if (!Cond)
      return true;
    if (containsLogicalOp(Cond))
      return true;
    SourceLocation SL = Cond->getBeginLoc();
    if (SL.isInvalid() || !SM.isWrittenInMainFile(SL))
      return true;
    unsigned Line = SM.getSpellingLineNumber(SL);
    std::string File = SM.getFilename(SL).str();
    auto [Norm, H] = makeCondNormAndHash(File, Line, Cond);
    bool Flip = condNormFlipped(Cond);
    std::string Inject = std::string("BrInfo::Runtime::LogCond(") +
                         BrInfo::toHex64(CurrentFuncHash) + ", \"" + File +
                         "\", " + std::to_string(Line) + ", (bool)(";
    R.InsertText(Cond->getBeginLoc(), Inject, true, true);
    std::string Suffix = std::string(") , \"") + escapeForCxxString(Norm) +
                         "\", " + BrInfo::toHex64(H) + ", " +
                         (Flip ? "true" : "false") + ")";
    R.InsertTextAfterToken(Cond->getEndLoc(), Suffix);
    return true;
  }

  bool VisitCaseStmt(CaseStmt *CS) {
    SourceLocation KL = CS->getKeywordLoc();
    if (KL.isInvalid() || !SM.isWrittenInMainFile(KL))
      return true;
    unsigned Line = SwitchStack.empty() ? SM.getSpellingLineNumber(KL)
                                        : SwitchStack.back().Line;
    SourceLocation Colon = CS->getColonLoc();
    if (Colon.isValid()) {
      std::string File = SwitchStack.empty() ? SM.getFilename(KL).str()
                                             : SwitchStack.back().File;
      std::string SwitchNorm =
          SwitchStack.empty() ? std::string("") : SwitchStack.back().SwitchNorm;
      std::string CaseVal = condNormFromExpr(CS->getLHS());
      std::string Norm = SwitchNorm.empty() ? (std::string("case ") + CaseVal)
                                            : (SwitchNorm + " == " + CaseVal);
      uint64_t H =
          BrInfo::hash64(File + ":" + std::to_string(Line) + ":" + Norm);
      std::string Inject = std::string(" BrInfo::Runtime::LogCond(") +
                           BrInfo::toHex64(CurrentFuncHash) + ", \"" + File +
                           "\", " + std::to_string(Line) + ", true, \"" +
                           escapeForCxxString(Norm) + "\", " +
                           BrInfo::toHex64(H) + ", false);";
      R.InsertTextAfterToken(Colon, Inject);
    }
    return true;
  }

  bool VisitDefaultStmt(DefaultStmt *DS) {
    SourceLocation KL = DS->getKeywordLoc();
    if (KL.isInvalid() || !SM.isWrittenInMainFile(KL))
      return true;
    unsigned Line = SwitchStack.empty() ? SM.getSpellingLineNumber(KL)
                                        : SwitchStack.back().Line;
    SourceLocation Colon = DS->getColonLoc();
    if (Colon.isValid()) {
      std::string File = SwitchStack.empty() ? SM.getFilename(KL).str()
                                             : SwitchStack.back().File;
      std::string SwitchNorm =
          SwitchStack.empty() ? std::string("") : SwitchStack.back().SwitchNorm;
      const std::vector<std::string> emptyCases;
      const std::vector<std::string> &Cases =
          SwitchStack.empty() ? emptyCases : SwitchStack.back().CaseNorms;
      std::string Norm;
      if (!SwitchNorm.empty() && !Cases.empty()) {
        Norm = SwitchNorm + " == " + Cases.front();
        for (size_t i = 1; i < Cases.size(); ++i) {
          Norm += " || " + SwitchNorm + " == " + Cases[i];
        }
      } else if (!SwitchNorm.empty()) {
        Norm = SwitchNorm;
      } else {
        Norm = "default";
      }
      uint64_t H =
          BrInfo::hash64(File + ":" + std::to_string(Line) + ":" + Norm);
      std::string Inject = std::string(" BrInfo::Runtime::LogCond(") +
                           BrInfo::toHex64(CurrentFuncHash) + ", \"" + File +
                           "\", " + std::to_string(Line) + ", true, \"" +
                           escapeForCxxString(Norm) + "\", " +
                           BrInfo::toHex64(H) + ", false);";
      R.InsertTextAfterToken(Colon, Inject);
    }
    return true;
  }

  bool VisitCXXForRangeStmt(CXXForRangeStmt *FR) {
    SourceLocation ForLoc = FR->getForLoc();
    if (ForLoc.isInvalid() || !SM.isWrittenInMainFile(ForLoc))
      return true;
    unsigned Line = SM.getSpellingLineNumber(ForLoc);
    std::string File = SM.getFilename(ForLoc).str();
    // Build a stable cond_norm from range-init if available
    std::string Norm;
    if (Stmt *Init = FR->getRangeInit()) {
      llvm::raw_string_ostream OS(Norm);
      Init->printPretty(OS, nullptr, LO);
      OS.flush();
      Norm = rtrimSemiSpace(Norm);
      Norm = std::string("range_for:") + Norm;
    } else {
      Norm = "range_for";
    }
    uint64_t H = BrInfo::hash64(File + ":" + std::to_string(Line) + ":" + Norm);
    std::string LogStmt = std::string("BrInfo::Runtime::LogCond(") +
                          BrInfo::toHex64(CurrentFuncHash) + ", \"" + File +
                          "\", " + std::to_string(Line) + ", true, \"" +
                          escapeForCxxString(Norm) + "\", " +
                          BrInfo::toHex64(H) + ", false);";

    Stmt *Body = FR->getBody();
    if (isa<CompoundStmt>(Body)) {
      // Insert just after '{'
      R.InsertTextAfterToken(Body->getBeginLoc(), LogStmt);
    } else {
      // Wrap single statement body into a compound and prepend our log
      R.InsertText(Body->getBeginLoc(), std::string("{ ") + LogStmt, true,
                   true);
      R.InsertTextAfterToken(Body->getEndLoc(), " }");
    }
    return true;
  }

  bool TraverseSwitchStmt(SwitchStmt *SS) {
    // Compute condition location to match meta's line/file for case/default
    SwitchCtx Ctx;
    if (Expr *Cond = SS->getCond()) {
      SourceLocation SL = Cond->getBeginLoc();
      if (SL.isValid()) {
        Ctx.Line = SM.getSpellingLineNumber(SL);
        Ctx.File = SM.getFilename(SL).str();
      }
    }
    // Precompute normalized switch condition
    if (Expr *Cond = SS->getCond()) {
      Ctx.SwitchNorm = condNormFromExpr(Cond);
    }
    // Collect case values in source order
    std::vector<std::pair<unsigned, std::string>> casesWithPos;
    for (SwitchCase *SC = SS->getSwitchCaseList(); SC;
         SC = SC->getNextSwitchCase()) {
      if (auto *CS = dyn_cast<CaseStmt>(SC)) {
        SourceLocation BL = CS->getBeginLoc();
        unsigned off = BL.isValid() ? SM.getFileOffset(BL) : 0u;
        casesWithPos.emplace_back(off, condNormFromExpr(CS->getLHS()));
      }
    }
    llvm::sort(casesWithPos.begin(), casesWithPos.end(),
               [](const auto &a, const auto &b) { return a.first < b.first; });
    for (auto &p : casesWithPos)
      Ctx.CaseNorms.push_back(std::move(p.second));
    SwitchStack.push_back(Ctx);
    bool Res = RecursiveASTVisitor::TraverseSwitchStmt(SS);
    SwitchStack.pop_back();
    return Res;
  }

  bool TraverseIfStmt(IfStmt *S) {
    if (!S)
      return true;
    if (!RecursiveASTVisitor::WalkUpFromIfStmt(S))
      return false;
    RecursiveASTVisitor::TraverseStmt(S->getInit());
    if (auto *VD = S->getConditionVariable())
      RecursiveASTVisitor::TraverseDecl(VD);
    ++CondDepth;
    RecursiveASTVisitor::TraverseStmt(S->getCond());
    --CondDepth;
    RecursiveASTVisitor::TraverseStmt(S->getThen());
    RecursiveASTVisitor::TraverseStmt(S->getElse());
    return true;
  }

  bool TraverseWhileStmt(WhileStmt *S) {
    if (!S)
      return true;
    if (!RecursiveASTVisitor::WalkUpFromWhileStmt(S))
      return false;
    ++CondDepth;
    RecursiveASTVisitor::TraverseStmt(S->getCond());
    --CondDepth;
    RecursiveASTVisitor::TraverseStmt(S->getBody());
    return true;
  }

  bool TraverseForStmt(ForStmt *S) {
    if (!S)
      return true;
    if (!RecursiveASTVisitor::WalkUpFromForStmt(S))
      return false;
    RecursiveASTVisitor::TraverseStmt(S->getInit());
    if (Stmt *Cond = S->getCond()) {
      ++CondDepth;
      RecursiveASTVisitor::TraverseStmt(Cond);
      --CondDepth;
    }
    RecursiveASTVisitor::TraverseStmt(S->getInc());
    RecursiveASTVisitor::TraverseStmt(S->getBody());
    return true;
  }

  bool TraverseDoStmt(DoStmt *S) {
    if (!S)
      return true;
    if (!RecursiveASTVisitor::WalkUpFromDoStmt(S))
      return false;
    RecursiveASTVisitor::TraverseStmt(S->getBody());
    ++CondDepth;
    RecursiveASTVisitor::TraverseStmt(S->getCond());
    --CondDepth;
    return true;
  }

  bool TraverseConditionalOperator(ConditionalOperator *S) {
    if (!S)
      return true;
    if (!RecursiveASTVisitor::WalkUpFromConditionalOperator(S))
      return false;
    ++CondDepth;
    RecursiveASTVisitor::TraverseStmt(S->getCond());
    --CondDepth;
    RecursiveASTVisitor::TraverseStmt(S->getTrueExpr());
    RecursiveASTVisitor::TraverseStmt(S->getFalseExpr());
    return true;
  }

  bool VisitBinaryOperator(BinaryOperator *BO) {
    if (CondDepth <= 0)
      return true;
    if (!BO->isLogicalOp())
      return true;
    // For each operand that is not itself a logical op, wrap it.
    auto wrapOperand = [&](Expr *Op) {
      Expr *E = Op->IgnoreParenImpCasts();
      if (auto *Inner = dyn_cast<BinaryOperator>(E)) {
        if (Inner->isLogicalOp())
          return; // will be handled at its own visit
      }
      SourceLocation SL = E->getBeginLoc();
      if (SL.isInvalid() || !SM.isWrittenInMainFile(SL))
        return;
      unsigned Line = SM.getSpellingLineNumber(SL);
      // Build cond_norm using strict normalization and compute flip flag
      std::string Norm = condNormFromExpr(E);
      bool Flip = condNormFlipped(E);
      std::string File = SM.getFilename(SL).str();
      uint64_t H =
          BrInfo::hash64(File + ":" + std::to_string(Line) + ":" + Norm);
      std::string Prefix = std::string("BrInfo::Runtime::LogCond(") +
                           BrInfo::toHex64(CurrentFuncHash) + ", \"" + File +
                           "\", " + std::to_string(Line) + ", (bool)(";
      R.InsertText(E->getBeginLoc(), Prefix, true, true);
      std::string Suffix = std::string(") , \"") + escapeForCxxString(Norm) +
                           "\", " + BrInfo::toHex64(H) + ", " +
                           (Flip ? "true" : "false") + ")";
      R.InsertTextAfterToken(E->getEndLoc(), Suffix);
    };
    wrapOperand(BO->getLHS());
    wrapOperand(BO->getRHS());
    return true;
  }
};

class IfInstrumentConsumer : public ASTConsumer {
  Rewriter &R;
  IfInstrumentVisitor Visitor;

public:
  explicit IfInstrumentConsumer(Rewriter &R) : R(R), Visitor(R) {}
  void HandleTranslationUnit(ASTContext &Context) override {
    Visitor.TraverseDecl(Context.getTranslationUnitDecl());
  }
};

class IfInstrumentAction : public ASTFrontendAction {
  Rewriter R;

public:
  IfInstrumentAction() {}
  void EndSourceFileAction() override {
    // Ensure include present at top
    auto &SM = R.getSourceMgr();
    FileID FID = SM.getMainFileID();
    SourceLocation Start = SM.getLocForStartOfFile(FID);
    // Only insert if not already present
    auto &Buf = R.getEditBuffer(FID);
    std::string Prefix;
    llvm::raw_string_ostream IncOS(Prefix);
    Buf.write(IncOS);
    IncOS.flush();
    if (Prefix.find("#include <brinfo/Runtime.h>") == std::string::npos &&
        Prefix.find("#include \"brinfo/Runtime.h\"") == std::string::npos) {
      R.InsertText(Start, "#include \"brinfo/Runtime.h\"\n", true, true);
    }
    // Write result
    std::error_code EC;
    std::string Out = OutputPath.empty()
                          ? SM.getFileEntryForID(FID)->getName().str() +
                                std::string(".inst.cpp")
                          : OutputPath.getValue();
    llvm::raw_fd_ostream OS(Out, EC, llvm::sys::fs::OF_Text);
    R.getEditBuffer(FID).write(OS);
  }
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef) override {
    R.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return std::make_unique<IfInstrumentConsumer>(R);
  }
};

int main(int argc, const char **argv) {
  auto ExpectedParser = CommonOptionsParser::create(argc, argv, InstrCat);
  if (!ExpectedParser) {
    llvm::errs() << ExpectedParser.takeError();
    return 1;
  }
  CommonOptionsParser &Options = ExpectedParser.get();
  ClangTool Tool(Options.getCompilations(), Options.getSourcePathList());
  return Tool.run(newFrontendActionFactory<IfInstrumentAction>().get());
}
