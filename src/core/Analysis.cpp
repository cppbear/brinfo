#include "brinfo/Analysis.h"
#include "brinfo/debug_macros.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Type.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <sstream>
#include <stack>

namespace BrInfo {

string formatID(string ID) {
  string Str;
  size_t Size = ID.size();
  if (Size < 3) {
    Str = string(3 - Size, '0') + ID;
  } else {
    Str = ID;
  }
  return Str;
}

void Analysis::setType(AnalysisType T) {
  Type = T;
  switch (T) {
  case FILE:
    Results["type"] = "file";
    break;
  case FUNC:
    Results["type"] = "function";
    break;
  }
}

bool Analysis::init(CFG *CFG, ASTContext *Context, const FunctionDecl *FD) {
  string Loc = FD->getLocation().printToString(Context->getSourceManager());
  if (VisitedFuncs.find(Loc) == VisitedFuncs.end()) {
    VisitedFuncs.insert(Loc);
  } else {
    return false;
  }
  Cfg = CFG;
  this->Context = Context;
  FocalFunc = FD;
  setSignature();
  CondChainForBlk.resize(Cfg->getNumBlockIDs());
  ColorOfBlk.resize(Cfg->getNumBlockIDs(), 0);
  CondChainForBlk[Cfg->getEntry().getBlockID()] = {
      {{nullptr, false, {}, {}}}, {&Cfg->getEntry()}, false, {}, {}};
  return true;
}

void Analysis::analyze() {
  extractCondChains();
  if (CondChains.size() < MaxChains) {

    CondChainList &ChainList = CondChains;
    for (CondChainInfo &ChainInfo : ChainList) {
      ChainInfo.analyze(Context);
    }
    condChainsToReqs();
  }
  clear();
}

void Analysis::dumpReqToJson(string ProjectPath, string FileName,
                             string ClassName, string FuncName) {
  string FilePath = ProjectPath + "/llm_reqs";
  llvm::sys::fs::create_directories(FilePath);
  if (Type == AnalysisType::FILE) {
    FilePath += "/" + FileName + "_req.json";
  } else {
    FilePath += "/" + (ClassName.empty() ? "" : ClassName + "_") + FuncName +
                "_req.json";
  }
  error_code EC;
  llvm::raw_fd_stream File(FilePath, EC);
  if (EC) {
    errs() << "Error: " << EC.message() << "\n";
    return;
  }
  File << Results.dump(4);
}

void Analysis::setSignature() {
  const FunctionDecl *CanonicalDecl = FocalFunc->getCanonicalDecl();
  Signature = CanonicalDecl->getReturnType().getAsString() + " ";
  if (CanonicalDecl->isCXXClassMember()) {
    Signature +=
        cast<CXXRecordDecl>(CanonicalDecl->getParent())->getNameAsString() +
        "::";
  }
  Signature += CanonicalDecl->getNameAsString() + "(";
  int I = 0;
  for (ParmVarDecl *Param : CanonicalDecl->parameters()) {
    if (I++ > 0) {
      Signature += ", ";
    }
    Signature += Param->getType().getAsString();
  }
  Signature += ")";
  DEBUG_PRINT("Signature: " << Signature);
}

void Analysis::extractCondChains() {
  dfsTraverseCFGLoop(-1, &Cfg->getEntry());
  if (CondChains.size() >= MaxChains)
    return;
  toBlack();
  for (const CFGBlock *Try : Cfg->try_blocks()) {
    long Parent = Try->getBlockID();
    CFGBlock *Blk = *Try->succ_begin();
    CondChainForBlk[Try->getBlockID()] = {
        {{nullptr, false, {}, {}}}, {Try}, false, {}, {}};
    dfsTraverseCFGLoop(Parent, Blk);
    toBlack();
  }
}

long Analysis::findBestCover(set<tuple<const Stmt *, string, bool>> &Uncovered,
                             const CondChainList &CondChains,
                             vector<bool> &Used) {
  unsigned MaxCover = 0;
  long Index = -1;
  for (unsigned I = 0; I < CondChains.size(); ++I) {
    if (Used[I])
      continue;
    unsigned Cover = 0;
    for (const tuple<const Stmt *, string, bool> &Cond :
         CondChains[I].getCondSet()) {
      if (Uncovered.find(Cond) != Uncovered.end()) {
        ++Cover;
      }
    }
    if (Cover > MaxCover) {
      MaxCover = Cover;
      Index = (long)I;
    }
  }
  return Index;
}

unordered_set<unsigned> Analysis::findMinCover() {
  set<tuple<const Stmt *, string, bool>> AllElements;
  unordered_set<unsigned> Cover;
  CondChainList CondChains = this->CondChains;
  CondChains.erase(remove_if(CondChains.begin(), CondChains.end(),
                             [](const CondChainInfo &ChainInfo) {
                               return ChainInfo.IsContra;
                             }),
                   CondChains.end());

  for (CondChainInfo &ChainInfo : CondChains) {
    AllElements.merge(ChainInfo.getCondSet());
  }

  set<tuple<const Stmt *, string, bool>> Uncovered(AllElements);
  vector<bool> Used(CondChains.size(), false);
  while (!Uncovered.empty()) {
    long Index = findBestCover(Uncovered, CondChains, Used);
    if (Index == -1)
      break;
    for (const tuple<const Stmt *, string, bool> &Cond :
         CondChains[Index].getCondSet()) {
      Uncovered.erase(Cond);
    }
    Cover.insert(Index);
    Used[Index] = true;
  }
  return Cover;
}

vector<string> split(const string &Str, char Delim) {
  vector<string> Tokens;
  stringstream Ss(Str);
  string Token;
  while (getline(Ss, Token, Delim)) {
    if (!Token.empty())
      Tokens.push_back(Token);
  }
  return Tokens;
}

string getNamespace(const Decl *D) {
  const DeclContext *DC = D->getDeclContext();
  string Namespace;
  while (DC) {
    if (const NamespaceDecl *ND = dyn_cast<NamespaceDecl>(DC)) {
      if (!Namespace.empty())
        Namespace = ND->getNameAsString() + "::" + Namespace;
      else
        Namespace = ND->getNameAsString();
    }
    DC = DC->getParent();
  }
  if (Namespace.empty())
    return "";
  return Namespace;
}

void Analysis::condChainsToReqs() {
  json Json;

  string BeginLoc =
      FocalFunc->getBeginLoc().printToString(Context->getSourceManager());
  string EndLoc =
      FocalFunc->getEndLoc().printToString(Context->getSourceManager());
  string BeginLine = split(BeginLoc, ':')[1];
  string EndLine = split(EndLoc, ':')[1];
  Json["loc"] = {stoi(BeginLine), stoi(EndLine)};

  CondChainList CondChains = this->CondChains;
  CondChains.erase(remove_if(CondChains.begin(), CondChains.end(),
                             [](const CondChainInfo &ChainInfo) {
                               return ChainInfo.IsContra;
                             }),
                   CondChains.end());
  unsigned Size = CondChains.size();
  unordered_set<unsigned> MinCover = findMinCover();

  const FunctionDecl *CanonicalDecl = FocalFunc->getCanonicalDecl();
  Json["operator"] = CanonicalDecl->isOverloadedOperator();
  Json["name"] = CanonicalDecl->getNameAsString();
  Json["namespace"] = getNamespace(CanonicalDecl);

  string FilePath = Context->getSourceManager()
                        .getFilename(CanonicalDecl->getLocation())
                        .str();
  SmallVector<char, 128> RealPath;
  sys::fs::real_path(FilePath, RealPath);
  string RealFilePath(RealPath.begin(), RealPath.end());
  Json["decl_file"] = RealFilePath;

  string Input = "";
  int I = 0;
  for (ParmVarDecl *Param : CanonicalDecl->parameters()) {
    if (I++ > 0) {
      Input += ", ";
    }
    Input +=
        Param->getNameAsString() + " is a " + Param->getType().getAsString();
  }
  Json["input"] = Input;

  string ClassName = "";
  if (CanonicalDecl->isCXXClassMember()) {
    ClassName =
        cast<CXXRecordDecl>(CanonicalDecl->getParent())->getNameAsString();
  }
  Json["class"] = ClassName;

  unsigned MinCoverNum = 0;
  // collect return strings for meta
  std::vector<std::string> ReturnStrs;
  ReturnStrs.reserve(Size);
  for (unsigned ID = 0; ID < Size; ++ID) {
    string CondChainStr = formatID(to_string(ID));
    string Result = "";
    if (CanonicalDecl->getReturnType() != Context->VoidTy) {
      Result = CondChains[ID].getReturnStr(
          Context, CanonicalDecl->getReturnType().getAsString());
    }
    ReturnStrs.push_back(Result);
    json J = CondChains[ID].toTestReqs(Context);
    if (!MinCover.empty() && MinCover.find(ID) == MinCover.end())
      J["mincover"] = false;
    else {
      J["mincover"] = true;
      ++MinCoverNum;
    }
    J["result"] = Result;
    if (CondChains[ID].Path[0]->getBlockID() == Cfg->getEntry().getBlockID()) {
      J["incatch"] = false;
    } else {
      J["incatch"] = true;
    }
    Json["chains"][CondChainStr] = J;
  }
  Json["chains"]["size"] = Json["chains"].size();
  Json["chains"]["mincover"] = MinCoverNum;
  Results[Signature] = Json;

  // meta collection integration (ignore contra chains already removed above)
  extern void metaRecordFunction(
      const FunctionDecl *, ASTContext *, const std::string &,
      const std::vector<CondChainInfo> &, const std::unordered_set<unsigned> &,
      const std::vector<std::string> &);
  metaRecordFunction(CanonicalDecl, Context, Signature, this->CondChains,
                     MinCover, ReturnStrs);
}

void Analysis::clear() {
  Cfg = nullptr;
  Context = nullptr;
  FocalFunc = nullptr;
  Signature.clear();
  CondChainForBlk.clear();
  CondChains.clear();
  ColorOfBlk.clear();
  LoopInner.clear();
}

void Analysis::toBlack() {
  for (unsigned char &Color : ColorOfBlk) {
    if (Color == 1) {
      Color = 2;
    }
  }
}

bool isSucc(const CFGBlock *Parent, const CFGBlock *Child) {
  for (CFGBlock::AdjacentBlock Succ : Parent->succs()) {
    if (Succ.isReachable() && Succ->getBlockID() == Child->getBlockID()) {
      return true;
    }
  }
  return false;
}

void Analysis::dfsTraverseCFGLoop(long Parent, CFGBlock *FirstBlk) {
  stack<BlkCond> Stack;
  Stack.push({Parent, FirstBlk});

  while (!Stack.empty()) {
    if (CondChains.size() >= MaxChains)
      break;

    BlkCond BlkCond = Stack.top();
    Stack.pop();

    long Parent = BlkCond.Parent;
    const CFGBlock *Blk = BlkCond.Block;
    BaseCond *Condition = BlkCond.Condition;
    bool Flag = BlkCond.Flag;
    bool InLoop = BlkCond.InLoop;
    vector<unsigned> LoopBlks = BlkCond.LoopBlks;
    unsigned ID = Blk->getBlockID();

    if (ColorOfBlk[ID] == 0)
      ColorOfBlk[ID] = 1;

    if (ColorOfBlk[ID] == 2 && ID != Cfg->getExit().getBlockID()) {
      Stack.push({Parent, &Cfg->getExit(), Condition});
      continue;
    }

    if (Parent != -1 && !CondChainForBlk[Parent].Path.empty()) {
      CondChainInfo &ChainInfo = CondChainForBlk[Parent];
      CondChain Chain = ChainInfo.Chain;
      BlkPath Path = ChainInfo.Path;

      BlkPath SortedPath = Path;
      llvm::sort(SortedPath.begin(), SortedPath.end());
      // Loop detected
      if (binary_search(SortedPath.begin(), SortedPath.end(), Blk)) {
        bool Traverse = false;
        for (CFGBlock::AdjacentBlock Adj : Blk->succs()) {
          if (Adj.isReachable()) {
            if (!binary_search(SortedPath.begin(), SortedPath.end(), Adj)) {
              if (LoopInner[Blk->getBlockID()].find(Adj->getBlockID()) !=
                  LoopInner[Blk->getBlockID()].end())
                continue;
              vector<unsigned> Loop = {};
              for (const CFGBlock *Blk : Path) {
                Loop.push_back(Blk->getBlockID());
              }
              Stack.push({Parent, Adj, Condition, Flag, true, Loop});
              Traverse = true;
            }
          }
        }
        if (!Traverse && InLoop) {
          unsigned PathSize = Path.size();
          for (unsigned I = PathSize - 1; I > 0; --I) {
            const CFGBlock *Succ = Path[I];
            const CFGBlock *Pred = Path[I - 1];
            bool IsSucc = isSucc(Pred, Succ);
            if (!IsSucc) {
              for (CFGBlock::AdjacentBlock Adj : Pred->succs()) {
                if (Adj.isReachable() && isSucc(Adj, Succ)) {
                  LoopInner[Adj->getBlockID()].insert(Succ->getBlockID());
                }
              }
              break;
            }
          }
        }
        continue;
      }

      Chain.push_back({Condition, Flag, {}, {}});
      Path.push_back(Blk);
      CondChainForBlk[ID] = {Chain, Path, false, {}, {}};
    }

    const Stmt *Terminator = Blk->getTerminatorStmt();
    if (Terminator) {
      switch (Terminator->getStmtClass()) {
      default:
        errs() << "Unhandled Terminator: " << Terminator->getStmtClassName()
               << "\n";
        break;
      case Stmt::ConditionalOperatorClass:
      case Stmt::BinaryOperatorClass:
      case Stmt::IfStmtClass: {
        BaseCond *Cond = nullptr;
        const Stmt *InnerCond = Blk->getTerminatorCondition();
        if (InnerCond) {
          Cond = new IfCond(cast<Expr>(InnerCond)->IgnoreParenImpCasts());
        }
        const CFGBlock::AdjacentBlock *Adj = Blk->succ_begin();
        if (Cond) {
          if (Adj->isReachable()) {
            Stack.push({ID, *Adj, Cond, true, InLoop, LoopBlks});
          }
          if (Blk->succ_size() == 2) {
            ++Adj;
            if (Adj->isReachable()) {
              Stack.push({ID, *Adj, Cond, false, InLoop, LoopBlks});
            }
          }
        } else if (Adj->isReachable()) {
          Stack.push({ID, *Adj, nullptr, false, InLoop, LoopBlks});
        }
        break;
      }
      case Stmt::SwitchStmtClass: {
        CFGBlock *DefaultBlk = nullptr;
        const Stmt *InnerCond = Blk->getTerminatorCondition();
        vector<const Stmt *> Cases;
        for (CFGBlock::AdjacentBlock Adj : Blk->succs()) {
          if (!Adj.isReachable())
            continue;
          Stmt *Label = Adj->getLabel();
          if (!Label) {
            DefaultBlk = Adj;
            continue;
          }
          BaseCond *Cond = nullptr;
          if (Label->getStmtClass() == Stmt::CaseStmtClass) {
            const Stmt *Case = cast<CaseStmt>(Label)->getLHS();
            Cases.push_back(Case);
            if (InnerCond) {
              Cond = new CaseCond(cast<Expr>(InnerCond)->IgnoreParenImpCasts(),
                                  Case);
            }
            Stack.push({ID, Adj, Cond, true, InLoop, LoopBlks});
          } else {
            // Default case
            assert(Label->getStmtClass() == Stmt::DefaultStmtClass);
            DefaultBlk = Adj;
          }
        }
        if (DefaultBlk) {
          BaseCond *Cond = nullptr;
          if (InnerCond) {
            Cond = new DefaultCond(InnerCond, Cases);
          }
          Stack.push({ID, DefaultBlk, Cond, false, InLoop, LoopBlks});
        }
        break;
      }
      case Stmt::CXXForRangeStmtClass:
      case Stmt::ForStmtClass:
      case Stmt::WhileStmtClass:
      case Stmt::DoStmtClass: {
        BaseCond *Cond = nullptr;
        const Stmt *InnerCond = Blk->getTerminatorCondition();
        if (InnerCond) {
          Cond = new LoopCond(cast<Expr>(InnerCond)->IgnoreParenImpCasts());
        }
        const CFGBlock::AdjacentBlock *Adj = Blk->succ_begin();
        if (InnerCond) {
          if (Adj->isReachable()) {
            Stack.push({ID, *Adj, Cond, true, InLoop, LoopBlks});
          }
          if (Blk->succ_size() == 2) {
            ++Adj;
            if (Adj->isReachable()) {
              Stack.push({ID, *Adj, Cond, false, InLoop, LoopBlks});
            }
          }
        } else if (Adj->isReachable()) {
          Stack.push({ID, *Adj, nullptr, false, InLoop, LoopBlks});
        }
        break;
      }
      case Stmt::BreakStmtClass:
      case Stmt::ContinueStmtClass:
      case Stmt::GotoStmtClass: {
        if (Blk->succ_begin()->isReachable()) {
          Stack.push(
              {ID, *Blk->succ_begin(), nullptr, false, InLoop, LoopBlks});
        }
        break;
      }
      }
    } else if (Blk->succ_size() == 1) {
      if (Blk->succ_begin()->isReachable()) {
        Stack.push({ID, *Blk->succ_begin(), nullptr, false, InLoop, LoopBlks});
      }
    } else if (Blk->getBlockID() == Cfg->getExit().getBlockID()) {
      // Exit Block
      CondChains.push_back(CondChainForBlk[ID]);
    } else {
      errs() << "Unhandle Block:\n";
      Blk->dump();
    }
  }
}

void Analysis::dumpCondChains() {
  unsigned Size = CondChains.size();
  for (unsigned I = 0; I < Size; ++I) {
    outs() << "CondChain " << I << ":\n";
    CondChains[I].dump(Context, 2);
  }
}

void Analysis::dumpBlkChain(unsigned ID) {
  CondChainForBlk[ID].dump(Context, 4);
}

void Analysis::dumpBlkChain() {
  for (unsigned I = 0; I < CondChainForBlk.size(); ++I) {
    outs() << "Block: " << I << "\n";
    dumpBlkChain(I);
  }
}

} // namespace BrInfo