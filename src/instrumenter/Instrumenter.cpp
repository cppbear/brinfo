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
  struct SwitchCtx {
    std::string File;
    unsigned Line = 0;
  };
  std::vector<SwitchCtx> SwitchStack;

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
    SourceLocation SL = Cond->getBeginLoc();
    if (SL.isInvalid() || !SM.isWrittenInMainFile(SL))
      return true;
    unsigned Line = SM.getSpellingLineNumber(SL);
    std::string Inject = std::string("BrInfo::Runtime::LogCond(") +
                         BrInfo::toHex64(CurrentFuncHash) + ", \"" +
                         SM.getFilename(SL).str() + "\", " +
                         std::to_string(Line) + ", (bool)(";
    R.InsertText(Cond->getBeginLoc(), Inject, true, true);
    R.InsertTextAfterToken(Cond->getEndLoc(), "))");
    return true;
  }

  bool VisitWhileStmt(WhileStmt *WS) {
    Expr *Cond = WS->getCond();
    if (!Cond)
      return true;
    SourceLocation SL = Cond->getBeginLoc();
    if (SL.isInvalid() || !SM.isWrittenInMainFile(SL))
      return true;
    unsigned Line = SM.getSpellingLineNumber(SL);
    std::string Inject = std::string("BrInfo::Runtime::LogCond(") +
                         BrInfo::toHex64(CurrentFuncHash) + ", \"" +
                         SM.getFilename(SL).str() + "\", " +
                         std::to_string(Line) + ", (bool)(";
    R.InsertText(Cond->getBeginLoc(), Inject, true, true);
    R.InsertTextAfterToken(Cond->getEndLoc(), "))");
    return true;
  }

  bool VisitForStmt(ForStmt *FS) {
    Expr *Cond = FS->getCond();
    if (!Cond)
      return true; // for(;;) has no condition
    SourceLocation SL = Cond->getBeginLoc();
    if (SL.isInvalid() || !SM.isWrittenInMainFile(SL))
      return true;
    unsigned Line = SM.getSpellingLineNumber(SL);
    std::string Inject = std::string("BrInfo::Runtime::LogCond(") +
                         BrInfo::toHex64(CurrentFuncHash) + ", \"" +
                         SM.getFilename(SL).str() + "\", " +
                         std::to_string(Line) + ", (bool)(";
    R.InsertText(Cond->getBeginLoc(), Inject, true, true);
    R.InsertTextAfterToken(Cond->getEndLoc(), "))");
    return true;
  }

  bool VisitDoStmt(DoStmt *DS) {
    Expr *Cond = DS->getCond();
    if (!Cond)
      return true;
    SourceLocation SL = Cond->getBeginLoc();
    if (SL.isInvalid() || !SM.isWrittenInMainFile(SL))
      return true;
    unsigned Line = SM.getSpellingLineNumber(SL);
    std::string Inject = std::string("BrInfo::Runtime::LogCond(") +
                         BrInfo::toHex64(CurrentFuncHash) + ", \"" +
                         SM.getFilename(SL).str() + "\", " +
                         std::to_string(Line) + ", (bool)(";
    R.InsertText(Cond->getBeginLoc(), Inject, true, true);
    R.InsertTextAfterToken(Cond->getEndLoc(), "))");
    return true;
  }

  bool VisitConditionalOperator(ConditionalOperator *CO) {
    Expr *Cond = CO->getCond();
    if (!Cond)
      return true;
    SourceLocation SL = Cond->getBeginLoc();
    if (SL.isInvalid() || !SM.isWrittenInMainFile(SL))
      return true;
    unsigned Line = SM.getSpellingLineNumber(SL);
    std::string Inject = std::string("BrInfo::Runtime::LogCond(") +
                         BrInfo::toHex64(CurrentFuncHash) + ", \"" +
                         SM.getFilename(SL).str() + "\", " +
                         std::to_string(Line) + ", (bool)(";
    R.InsertText(Cond->getBeginLoc(), Inject, true, true);
    R.InsertTextAfterToken(Cond->getEndLoc(), "))");
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
      std::string Inject = std::string(" BrInfo::Runtime::LogCond(") +
                           BrInfo::toHex64(CurrentFuncHash) + ", \"" + File +
                           "\", " + std::to_string(Line) + ", true);";
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
      std::string Inject = std::string(" BrInfo::Runtime::LogCond(") +
                           BrInfo::toHex64(CurrentFuncHash) + ", \"" + File +
                           "\", " + std::to_string(Line) + ", true);";
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
    std::string LogStmt = std::string("BrInfo::Runtime::LogCond(") +
                          BrInfo::toHex64(CurrentFuncHash) + ", \"" + File +
                          "\", " + std::to_string(Line) + ", true);\n";

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
    SwitchStack.push_back(Ctx);
    bool Res = RecursiveASTVisitor::TraverseSwitchStmt(SS);
    SwitchStack.pop_back();
    return Res;
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
