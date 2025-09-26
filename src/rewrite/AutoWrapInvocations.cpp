#include <clang/AST/AST.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <clang/Lex/Lexer.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <clang/Frontend/CompilerInstance.h>
#include <iostream>
#include <llvm/Support/CommandLine.h>
#include <regex>

using namespace clang;
using namespace clang::tooling;

static llvm::cl::OptionCategory BrInfoCat("brinfo-callwrap options");
static llvm::cl::opt<std::string> AllowRegex(
    "allow", llvm::cl::desc("regex of fully qualified function names to wrap"),
    llvm::cl::init(""), llvm::cl::cat(BrInfoCat));
static llvm::cl::opt<bool> OnlyTests(
    "only-tests", llvm::cl::desc("limit to gtest TestBody functions"),
    llvm::cl::init(true), llvm::cl::cat(BrInfoCat));

namespace {

class CallWrapVisitor : public RecursiveASTVisitor<CallWrapVisitor> {
public:
  CallWrapVisitor(ASTContext &Ctx, Rewriter &R)
      : Ctx(Ctx), R(R) {
    if (!AllowRegex.empty())
      Allow = std::regex(AllowRegex);
  }

  bool TraverseFunctionDecl(FunctionDecl *FD) {
    // Track whether we're inside a gtest-generated TestBody
    bool PrevInTest = InTestBody;
    if (OnlyTests && FD && FD->isThisDeclarationADefinition()) {
      std::cout << "Examining function: " << FD->getNameAsString() << "\n";
      if (FD->getNameAsString() == "TestBody")
        InTestBody = true;
    }
    bool Result = RecursiveASTVisitor::TraverseFunctionDecl(FD);
    InTestBody = PrevInTest;
    return Result;
  }

  bool VisitCallExpr(CallExpr *CE) {
    // Skip in system headers / macros
    const SourceManager &SM = Ctx.getSourceManager();
    SourceLocation Loc = CE->getExprLoc();
    if (Loc.isInvalid() || SM.isInSystemHeader(Loc) || Loc.isMacroID())
      return true;

    // Optional: limit to gtest TestBody
    if (OnlyTests && !InTestBody)
      return true;

    // Resolve callee name for allowlist check
    const FunctionDecl *FD = CE->getDirectCallee();
    if (!FD)
      return true;
    std::string QName = FD->getQualifiedNameAsString();
    std::cout << "Considering call to: " << QName << "\n";
    if (AllowRegex.getNumOccurrences() && !std::regex_search(QName, Allow))
      return true;

  // Get source for the call expression
  CharSourceRange Range = CharSourceRange::getTokenRange(CE->getSourceRange());
    llvm::StringRef Text = Lexer::getSourceText(Range, SM, Ctx.getLangOpts());
    if (Text.empty()) return true;

    // Build wrapped text: BRINFO_CALL(<original>)
    std::string Wrapped = ("BRINFO_CALL(" + Text.str() + ")");
    R.ReplaceText(Range, Wrapped);
    return true;
  }

private:
  ASTContext &Ctx;
  Rewriter &R;
  std::regex Allow;
  bool InTestBody = false;
};

class CallWrapConsumer : public ASTConsumer {
public:
  CallWrapConsumer(ASTContext &Ctx, Rewriter &R) : Visitor(Ctx, R) {}
  void HandleTranslationUnit(ASTContext &Ctx) override {
    Visitor.TraverseDecl(Ctx.getTranslationUnitDecl());
  }
private:
  CallWrapVisitor Visitor;
};

class CallWrapAction : public ASTFrontendAction {
public:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef InFile) override {
    R.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return std::make_unique<CallWrapConsumer>(CI.getASTContext(), R);
  }
  void EndSourceFileAction() override {
    R.overwriteChangedFiles();
  }
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
  ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());
  return Tool.run(newFrontendActionFactory<CallWrapAction>().get());
}
