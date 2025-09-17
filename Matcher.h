#pragma once

#include "Analysis.h"
#include "CFGDOT.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Execution.h"

using namespace clang::ast_matchers;
using namespace clang::tooling;

extern cl::opt<string> FunctionName;
extern cl::opt<string> ClassName;
extern cl::opt<bool> DumpCFG;
extern string RealProjectPath;

namespace BrInfo {

class FuncAnalysis : public MatchFinder::MatchCallback {
  Analysis &Analyzer;
  string FilePath;

  bool isLambdaFunction(const FunctionDecl *FD) {
    if (const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(FD)) {
      if (const CXXRecordDecl *RD = MD->getParent()) {
        return RD->isLambda();
      }
    }
    return false;
  }

public:
  FuncAnalysis(Analysis &Analyzer, string FilePath)
      : Analyzer(Analyzer), FilePath(FilePath) {}

  virtual void run(const MatchFinder::MatchResult &Result) override {
    if (const FunctionDecl *Func =
            Result.Nodes.getNodeAs<FunctionDecl>("func")) {
      // Skip main and test functions
      if (Func->getNameAsString().empty() ||
          Func->getNameAsString().find("main") != string::npos ||
          Func->getNameAsString().find("test") != string::npos)
        return;
      string FileName =
          Result.SourceManager->getFilename(Func->getLocation()).str();
      SmallVector<char, 128> RealPath;
      sys::fs::real_path(FileName, RealPath);
      string ResolvedPath(RealPath.begin(), RealPath.end());
      // Skip if the file is not the same as the one we are analyzing
      if (FilePath != ResolvedPath)
        return;
      // Skip constructors, destructors and lambda functions
      if (Func->getDeclKind() == Decl::CXXConstructor ||
          Func->getDeclKind() == Decl::CXXDestructor || isLambdaFunction(Func))
        return;

      auto BO = CFG::BuildOptions();
      BO.PruneTriviallyFalseEdges = true;
      auto Cfg = CFG::buildCFG(Func, Func->getBody(), Result.Context, BO);
      if (Cfg) {
        bool Flag = Analyzer.init(Cfg.get(), Result.Context, Func);
        if (!Flag)
          return;
        if (DumpCFG)
          dumpCFGToDot(Cfg.get(), Result.Context->getLangOpts(), RealProjectPath,
                            Analyzer.getSignature(),
                            Func->getNameAsString());
        Analyzer.analyze();
      }
    }
  }
};

inline string getFileName(string SourcePath) {
  size_t Pos = SourcePath.find_last_of("/");
  if (Pos != string::npos)
    return SourcePath.substr(Pos + 1);
  return SourcePath;
}

inline int run(ClangTool &Tool) {
  string FileName = getFileName(Tool.getSourcePaths()[0]);
  SmallVector<char, 128> RealPath;
  sys::fs::real_path(Tool.getSourcePaths()[0], RealPath);
  string FilePath(RealPath.begin(), RealPath.end());
  Analysis Analyzer;
  FuncAnalysis FuncAnalyzer(Analyzer, FilePath);
  MatchFinder Finder;
  if (!FunctionName.empty()) {
    Analyzer.setType(AnalysisType::FUNC);
    if (!ClassName.empty()) {
      DeclarationMatcher FuncMatcher =
          cxxMethodDecl(isDefinition(), hasName(FunctionName),
                        ofClass(hasName(ClassName)))
              .bind("func");
      Finder.addMatcher(FuncMatcher, &FuncAnalyzer);
    } else {
      DeclarationMatcher FuncMatcher =
          functionDecl(isDefinition(), hasName(FunctionName)).bind("func");
      Finder.addMatcher(FuncMatcher, &FuncAnalyzer);
    }
  } else {
    Analyzer.setType(AnalysisType::FILE);
    DeclarationMatcher FuncMatcher = functionDecl(isDefinition()).bind("func");
    Finder.addMatcher(FuncMatcher, &FuncAnalyzer);
  }
  int Res = Tool.run(newFrontendActionFactory(&Finder).get());
  Analyzer.dumpReqToJson(RealProjectPath, FileName, ClassName, FunctionName);
  return Res;
}

} // namespace BrInfo
