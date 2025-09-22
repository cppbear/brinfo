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
// hash64 provided via brinfo/Utils.h; no direct xxhash include needed here

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

  // Use BrInfo::hash64 from Utils.h

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
