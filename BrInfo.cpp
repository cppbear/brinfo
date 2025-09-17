#include "Matcher.h"

// Set up the command line options
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
static cl::OptionCategory BrInfoCategory("brinfo options");
cl::opt<string> FunctionName("f",
                                  cl::desc("Specify the function to analyze"),
                                  cl::value_desc("string"),
                                  cl::cat(BrInfoCategory));
cl::opt<string> ClassName("c",
                               cl::desc("Specify the class of the function"),
                               cl::value_desc("string"),
                               cl::cat(BrInfoCategory));
static cl::opt<string> ProjectPath("project", cl::Required,
                                 cl::desc("Specify the project path"),
                                 cl::value_desc("string"),
                                 cl::cat(BrInfoCategory));
cl::opt<bool> DumpCFG("cfg", cl::desc("Dump CFG to .dot file"),
                      cl::cat(BrInfoCategory));

string RealProjectPath;

int main(int argc, const char **argv) {
  auto ExpectedParser = CommonOptionsParser::create(argc, argv, BrInfoCategory);
  if (!ExpectedParser) {
    errs() << ExpectedParser.takeError();
    return 1;
  }
  CommonOptionsParser &OptionParser = ExpectedParser.get();
  if (OptionParser.getSourcePathList().size() != 1) {
    errs() << "Just specify one source file\n";
    return 1;
  }
  SmallVector<char, 128> RealPath;
  sys::fs::real_path(ProjectPath, RealPath);
  string ProjectPathStr(RealPath.begin(), RealPath.end());
  RealProjectPath = ProjectPathStr;
  ClangTool Tool(OptionParser.getCompilations(),
                 OptionParser.getSourcePathList());

  return BrInfo::run(Tool);
}
