#include <clang/AST/AST.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Lex/Lexer.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>
#include <regex>

using namespace clang;
using namespace clang::tooling;

static llvm::cl::OptionCategory BrInfoCat("brinfo-callwrap options");
static llvm::cl::opt<std::string> AllowRegex(
    "allow", llvm::cl::desc("regex of fully qualified function names to wrap"),
    llvm::cl::init(""), llvm::cl::cat(BrInfoCat));
static llvm::cl::opt<bool>
    OnlyTests("only-tests", llvm::cl::desc("limit to gtest TestBody functions"),
              llvm::cl::init(true), llvm::cl::cat(BrInfoCat));
static llvm::cl::opt<bool> MainFileOnly(
    "main-file-only",
    llvm::cl::desc("detect TestBody only when its expansion is in main file"),
    llvm::cl::init(true), llvm::cl::cat(BrInfoCat));
static llvm::cl::opt<bool> WrapMacroArgs(
    "wrap-macro-args",
    llvm::cl::desc("allow wrapping call expressions appearing inside macro "
                   "arguments when their spelling is in the main file"),
    llvm::cl::init(false), llvm::cl::cat(BrInfoCat));

namespace {

class CallWrapVisitor : public RecursiveASTVisitor<CallWrapVisitor> {
public:
  CallWrapVisitor(ASTContext &Ctx, Rewriter &R) : Ctx(Ctx), R(R) {
    if (!AllowRegex.empty())
      Allow = std::regex(AllowRegex);
  }

  bool didModifyMainFile() const { return DidModifyMainFile; }

  // Check if the call range is already wrapped by BRINFO_CALL(
  bool isAlreadyWrapped(const CharSourceRange &Range, const SourceManager &SM) {
    const LangOptions &LO = Ctx.getLangOpts();
    SourceLocation B = Range.getBegin();
    if (B.isInvalid())
      return false;
    SourceLocation BFile = SM.getFileLoc(B);
    if (BFile.isInvalid())
      return false;
    FileID FID = SM.getFileID(BFile);
    unsigned Off = SM.getFileOffset(BFile);
    unsigned Lookback = Off > 48 ? 48 : Off; // look back up to 48 chars
    SourceLocation Start = BFile.getLocWithOffset(-(int)Lookback);
    CharSourceRange PreRange = CharSourceRange::getCharRange(Start, BFile);
    llvm::StringRef Pre = Lexer::getSourceText(PreRange, SM, LO);
    // Trim trailing whitespace
    Pre = Pre.rtrim();
    return Pre.endswith("BRINFO_CALL(");
  }

  // Helper to robustly detect a gtest TestBody definition
  bool isGTestTestBody(const FunctionDecl *FD) {
    const auto *MD = llvm::dyn_cast<CXXMethodDecl>(FD);
    if (!MD || MD->getNameAsString() != "TestBody")
      return false;

    // Limit to functions written in the main file (after macro expansion)
    const SourceManager &SM = Ctx.getSourceManager();
    SourceLocation Loc = SM.getExpansionLoc(MD->getBeginLoc());
    if (MainFileOnly && !SM.isWrittenInMainFile(Loc))
      return false;

    // 1) Prefer the authoritative signal: overrides testing::Test::TestBody
    for (const CXXMethodDecl *OM : MD->overridden_methods()) {
      const CXXMethodDecl *Base = OM->getCanonicalDecl();
      std::string BaseQN = Base->getQualifiedNameAsString();
      if (BaseQN == "testing::Test::TestBody" ||
          BaseQN == "::testing::Test::TestBody") {
        return true;
      }
    }

    // 2) Fallback: the enclosing class derives (transitively) from
    // testing::Test
    const auto *CR = llvm::dyn_cast<CXXRecordDecl>(MD->getParent());
    std::function<bool(const CXXRecordDecl *)> derivesFromTestingTest =
        [&](const CXXRecordDecl *RD) -> bool {
      if (!RD || !RD->hasDefinition())
        return false;
      for (const CXXBaseSpecifier &BaseSpec : RD->bases()) {
        QualType BT = BaseSpec.getType();
        if (const auto *RT = BT->getAs<RecordType>()) {
          const auto *BRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl());
          if (!BRD)
            continue;
          std::string QN = BRD->getQualifiedNameAsString();
          if (QN == "testing::Test" || QN == "::testing::Test")
            return true;
          if (derivesFromTestingTest(BRD))
            return true;
        }
      }
      return false;
    };
    if (CR && derivesFromTestingTest(CR))
      return true;

    // 3) Last-resort heuristic: gtest macro-generated class name pattern *_Test
    if (CR) {
      std::string ClassName = CR->getNameAsString();
      if (llvm::StringRef(ClassName).endswith("_Test"))
        return true;
    }

    return false;
  }

  bool TraverseFunctionDecl(FunctionDecl *FD) {
    // Track whether we're inside a gtest-generated TestBody
    bool PrevInTest = InTestBody;
    if (OnlyTests && FD && FD->isThisDeclarationADefinition()) {
      if (isGTestTestBody(FD)) {
        InTestBody = true;
      }
    }
    bool Result = RecursiveASTVisitor::TraverseFunctionDecl(FD);
    InTestBody = PrevInTest;
    return Result;
  }

  bool TraverseCXXMethodDecl(CXXMethodDecl *MD) {
    // Track TestBody scope for method definitions
    bool PrevInTest = InTestBody;
    if (OnlyTests && MD && MD->isThisDeclarationADefinition()) {
      if (isGTestTestBody(MD)) {
        InTestBody = true;
      }
    }
    bool Result = RecursiveASTVisitor::TraverseCXXMethodDecl(MD);
    InTestBody = PrevInTest;
    return Result;
  }

  // Post-order wrapping: visit children first, then wrap this call
  bool TraverseCallExpr(CallExpr *CE) {
    if (!RecursiveASTVisitor::TraverseCallExpr(CE))
      return false;
    return wrapCall(CE);
  }

  bool wrapCall(CallExpr *CE) {
    const SourceManager &SM = Ctx.getSourceManager();
    SourceLocation Loc = CE->getExprLoc();
    if (Loc.isInvalid() || SM.isInSystemHeader(Loc))
      return true;

    // Optional: limit to gtest TestBody
    if (OnlyTests && !InTestBody)
      return true;

    // Resolve callee name for allowlist check
    const FunctionDecl *FD = CE->getDirectCallee();
    if (!FD)
      return true;
    std::string QName = FD->getQualifiedNameAsString();
    if (AllowRegex.getNumOccurrences() && !std::regex_search(QName, Allow))
      return true;

    const LangOptions &LO = Ctx.getLangOpts();
    CharSourceRange TokRange =
        CharSourceRange::getTokenRange(CE->getSourceRange());

    // Build a precise character range for replacement.
    CharSourceRange Range;
    if (Loc.isMacroID()) {
      // Inside macro: only wrap if enabled and spelled in main file.
      llvm::StringRef MacroName = Lexer::getImmediateMacroName(Loc, SM, LO);
      if (MacroName == "BRINFO_CALL")
        return true; // already inside wrapper
      if (!WrapMacroArgs)
        return true;
      SourceLocation B = SM.getSpellingLoc(TokRange.getBegin());
      SourceLocation EToken = SM.getSpellingLoc(TokRange.getEnd());
      if (!SM.isWrittenInMainFile(B))
        return true;
      SourceLocation E = Lexer::getLocForEndOfToken(EToken, 0, SM, LO);
      Range = CharSourceRange::getCharRange(B, E);
    } else {
      // Normal path: prefer a file char range
      Range = Lexer::makeFileCharRange(TokRange, SM, LO);
      if (Range.isInvalid()) {
        SourceLocation B = SM.getSpellingLoc(TokRange.getBegin());
        SourceLocation EToken = SM.getSpellingLoc(TokRange.getEnd());
        SourceLocation E = Lexer::getLocForEndOfToken(EToken, 0, SM, LO);
        Range = CharSourceRange::getCharRange(B, E);
      }
    }

    if (Range.isInvalid())
      return true;

    // Prevent double-wrapping if already under BRINFO_CALL in file text
    if (isAlreadyWrapped(Range, SM))
      return true;

    // Prefer rewritten text to include any prior inner-call wraps
    std::string Curr = R.getRewrittenText(Range);
    if (Curr.empty()) {
      llvm::StringRef Orig = Lexer::getSourceText(Range, SM, LO);
      Curr = Orig.str();
    }
    if (Curr.empty())
      return true;

    std::string Wrapped = ("BRINFO_CALL(" + Curr + ")");
    R.ReplaceText(Range, Wrapped);

    SourceLocation BFile = SM.getFileLoc(Range.getBegin());
    if (SM.isWrittenInMainFile(BFile))
      DidModifyMainFile = true;

    return true;
  }

private:
  ASTContext &Ctx;
  Rewriter &R;
  std::regex Allow;
  bool InTestBody = false;
  bool DidModifyMainFile = false;
};

class CallWrapConsumer : public ASTConsumer {
public:
  CallWrapConsumer(ASTContext &Ctx, Rewriter &R)
      : Ctx(Ctx), R(R), Visitor(Ctx, R) {}
  void HandleTranslationUnit(ASTContext &Ctx) override {
    Visitor.TraverseDecl(Ctx.getTranslationUnitDecl());

    // After wrapping calls, ensure we inject the auto-wrap macro and includes
    // at the very top of the main file, but only once and only if we modified
    // it.
    if (!Visitor.didModifyMainFile())
      return;

    const SourceManager &SM = Ctx.getSourceManager();
    FileID MainFID = SM.getMainFileID();
    if (MainFID.isInvalid())
      return;

    SourceLocation FileStart = SM.getLocForStartOfFile(MainFID);
    SourceLocation FileEnd = SM.getLocForEndOfFile(MainFID);
    if (FileStart.isInvalid() || FileEnd.isInvalid())
      return;

    const LangOptions &LO = Ctx.getLangOpts();
    CharSourceRange Whole = CharSourceRange::getCharRange(FileStart, FileEnd);
    llvm::StringRef FileText = Lexer::getSourceText(Whole, SM, LO);

    // Idempotency: if any marker already exists, skip insertion.
    if (FileText.contains("BRINFO_AUTO_WRAP_GTEST") ||
        FileText.contains("brinfo/GTestAutoWrap.h") ||
        FileText.contains("brinfo/GTestSupport.h")) {
      return;
    }

    std::string HeaderBlock;
    HeaderBlock += "#define BRINFO_AUTO_WRAP_GTEST\n";
    HeaderBlock += "#include \"brinfo/GTestAutoWrap.h\"\n";
    HeaderBlock += "#include \"brinfo/GTestSupport.h\"\n\n";

    R.InsertTextBefore(FileStart, HeaderBlock);
  }

private:
  ASTContext &Ctx;
  Rewriter &R;
  CallWrapVisitor Visitor;
};

class CallWrapAction : public ASTFrontendAction {
public:
  std::unique_ptr<ASTConsumer>
  CreateASTConsumer(CompilerInstance &CI, llvm::StringRef InFile) override {
    R.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return std::make_unique<CallWrapConsumer>(CI.getASTContext(), R);
  }
  void EndSourceFileAction() override { R.overwriteChangedFiles(); }

private:
  Rewriter R;
};

} // namespace

int main(int argc, const char **argv) {
  auto ExpectedParser = CommonOptionsParser::create(argc, argv, BrInfoCat);
  if (!ExpectedParser) {
    llvm::errs() << ExpectedParser.takeError();
    return 1;
  }
  CommonOptionsParser &OptionsParser = ExpectedParser.get();
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());
  return Tool.run(newFrontendActionFactory<CallWrapAction>().get());
}
