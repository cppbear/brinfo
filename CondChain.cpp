#include "CondChain.h"
#include "OrderedSet.h"
#include "clang/Basic/SourceManager.h"

extern string RealProjectPath;

namespace BrInfo {

OrderedSet<const CallExpr *> getCallExpr(const Stmt *S) {
  OrderedSet<const CallExpr *> Set;
  switch (S->getStmtClass()) {
  default:
    break;
  case Stmt::CXXMemberCallExprClass: {
  }
    LLVM_FALLTHROUGH;
  case Stmt::CallExprClass: {
    if (!Set.contains(cast<CallExpr>(S))) {
      Set.insert(cast<CallExpr>(S));
    }
    break;
  }
  case Stmt::BinaryOperatorClass: {
    const BinaryOperator *BO = cast<BinaryOperator>(S);
    Set.merge(getCallExpr(BO->getLHS()->IgnoreParenImpCasts()));
    Set.merge(getCallExpr(BO->getRHS()->IgnoreParenImpCasts()));
    break;
  }
  case Stmt::UnaryOperatorClass: {
    const UnaryOperator *UO = cast<UnaryOperator>(S);
    Set.merge(getCallExpr(UO->getSubExpr()->IgnoreParenImpCasts()));
    break;
  }
  }
  return Set;
}

bool checkLiteralExpr(const Expr *Expr, CondStatus &CondStatus) {
  bool IsNot = CondStatus.Condition->isNot();
  bool Flag = CondStatus.Flag;
  bool Res = true;
  switch (Expr->getStmtClass()) {
  default:
    break;
  case Stmt::CXXBoolLiteralExprClass: {
    const CXXBoolLiteralExpr *BLE = cast<CXXBoolLiteralExpr>(Expr);
    if ((Flag == BLE->getValue()) == IsNot) {
      Res = false;
    }
    break;
  }
  case Stmt::GNUNullExprClass:
  case Stmt::CXXNullPtrLiteralExprClass: {
    string CondStr = CondStatus.Condition->getCondStr();
    bool ContainNull = CondStr.find("nullptr") != string::npos ||
                       CondStr.find("NULL") != string::npos;
    if ((ContainNull == Flag) == IsNot) {
      Res = false;
    }
    break;
  }
  }
  return Res;
}

bool simplify(CondSimp &CondSimp, const BinaryOperator *BO, bool Flag);

// derive the condition from the parent to the child
bool deriveCond(CondSimp &CondSimp, bool Flag, BinaryOperator::Opcode Opcode,
                const Expr *Known, const Expr *Unknown) {
  bool Val = CondSimp.Map[Known];
  switch (Opcode) {
  case BinaryOperatorKind::BO_LAnd:
    if (Val) {
      CondSimp.Map[Unknown] = Flag;
      if (Unknown->getStmtClass() == Stmt::BinaryOperatorClass &&
          cast<BinaryOperator>(Unknown)->isLogicalOp()) {
        simplify(CondSimp, cast<BinaryOperator>(Unknown), Flag);
      } else {
        const Stmt *S = static_cast<const Stmt *>(Unknown);
        BaseCond *Cond = new IfCond(S);
        CondSimp.Cond = Cond;
        CondSimp.Flag = Flag;
      }
    } else if (Flag) {
      return false;
    }
    break;
  case BinaryOperatorKind::BO_LOr:
    if (!Val) {
      CondSimp.Map[Unknown] = Flag;
      if (Unknown->getStmtClass() == Stmt::BinaryOperatorClass &&
          cast<BinaryOperator>(Unknown)->isLogicalOp()) {
        simplify(CondSimp, cast<BinaryOperator>(Unknown), Flag);
      } else {
        const Stmt *S = static_cast<const Stmt *>(Unknown);
        BaseCond *Cond = new IfCond(S);
        CondSimp.Cond = Cond;
        CondSimp.Flag = Flag;
      }
    } else if (!Flag) {
      return false;
    }
    break;
  default:
    break;
  }
  return true;
}

// transfer the condition from the children to the parent
bool transferCond(CondSimp &CondSimp, const Expr *Parent) {
  bool Res = false;
  if (Parent->getStmtClass() == Stmt::BinaryOperatorClass) {
    const BinaryOperator *BO = cast<BinaryOperator>(Parent);
    if (BO->isLogicalOp()) {
      Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
      Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
      if (CondSimp.Map.find(LHS) == CondSimp.Map.end()) {
        transferCond(CondSimp, LHS);
      }
      if (CondSimp.Map.find(RHS) == CondSimp.Map.end()) {
        transferCond(CondSimp, RHS);
      }
      bool LHSKnown = CondSimp.Map.find(LHS) != CondSimp.Map.end();
      bool RHSKnown = CondSimp.Map.find(RHS) != CondSimp.Map.end();
      switch (BO->getOpcode()) {
      case BinaryOperatorKind::BO_LAnd:
        if (LHSKnown && RHSKnown) {
          CondSimp.Map[Parent] = CondSimp.Map[LHS] && CondSimp.Map[RHS];
          Res = true;
        } else if ((LHSKnown && !CondSimp.Map[LHS]) ||
                   (RHSKnown && !CondSimp.Map[RHS])) {
          CondSimp.Map[Parent] = false;
          Res = true;
        }
        break;
      case BinaryOperatorKind::BO_LOr:
        if (LHSKnown && RHSKnown) {
          CondSimp.Map[Parent] = CondSimp.Map[LHS] || CondSimp.Map[RHS];
          Res = true;
        } else if ((LHSKnown && CondSimp.Map[LHS]) ||
                   (RHSKnown && CondSimp.Map[RHS])) {
          CondSimp.Map[Parent] = true;
          Res = true;
        }
        break;
      default:
        break;
      }
    }
  }
  return Res;
}

bool simplify(CondSimp &CondSimp, const BinaryOperator *BO, bool Flag) {
  bool Ret = true;
  Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
  Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
  bool LHSKnown = CondSimp.Map.find(LHS) != CondSimp.Map.end();
  bool RHSKnown = CondSimp.Map.find(RHS) != CondSimp.Map.end();
  if (LHSKnown && !RHSKnown) {
    Ret = deriveCond(CondSimp, Flag, BO->getOpcode(), LHS, RHS);
  } else if (!LHSKnown && RHSKnown) {
    Ret = deriveCond(CondSimp, Flag, BO->getOpcode(), RHS, LHS);
  } else if (!LHSKnown && !RHSKnown) {
    bool Res = transferCond(CondSimp, LHS) || transferCond(CondSimp, RHS);
    if (Res) {
      Ret = simplify(CondSimp, BO, Flag);
    } else {
      errs() << "Unhandle condition\n";
      Ret = false;
    }
  }
  return Ret;
}

string CondStatus::toString() {
  string Str = "";
  Str += Condition->getCondStr() + " is ";
  if (Condition->isNot())
    Str += (Flag ? "false" : "true");
  else
    Str += (Flag ? "true" : "false");
  return Str;
}

StringList CondStatus::getLastDefStrVec(ASTContext *Context) {
  StringList StrVec;
  string Str;
  raw_string_ostream OS(Str);
  if (!LastDefStmts.empty() && !isa<LoopCond>(Condition)) {
    for (const Stmt *S : LastDefStmts) {
      S->printPretty(OS, nullptr, Context->getPrintingPolicy());
      OS.flush();
      rtrim(Str);
      StrVec.push_back(Str);
      Str.clear();
    }
  }
  return StrVec;
}

void CondStatus::dump(ASTContext *Context) {
  if (Condition) {
    Condition->dump(Context);
    outs() << " is " << (Flag ? "true" : "false");
    if (!LastDefStmts.empty() || !ParmVars.empty()) {
      string Str;
      raw_string_ostream OS(Str);
      outs() << ", where: ";
      unsigned I = 0;
      for (const Stmt *S : LastDefStmts) {
        S->printPretty(OS, nullptr, Context->getPrintingPolicy());
        OS.flush();
        rtrim(Str);
        if (I++ > 0)
          outs() << ", ";
        outs() << Str;
        Str.clear();
      }
      I = 0;
      for (const ParmVarDecl *PVD : ParmVars) {
        if (I++ > 0)
          outs() << ", ";
        outs() << PVD->getNameAsString() << " is ParmVar";
      }
    }
  }
}

void CondChainInfo::analyze(ASTContext *Context) {
  simplifyConds();
  if (IsContra)
    return;
  setCondStr(Context);
  findCallExprs();
  traceBack();
  findContra();
}

void CondChainInfo::simplifyConds() {
  CondSimp CondSimp;
  for (CondStatus &Cond : Chain) {
    if (Cond.Condition) {
      CondSimp.Cond = nullptr;
      const Stmt *S = Cond.Condition->getCond();
      CondSimp.Map[S] = Cond.Flag;

      if (S->getStmtClass() == Stmt::BinaryOperatorClass) {
        const BinaryOperator *BO = cast<BinaryOperator>(S);
        if (BO->isLogicalOp()) {
          bool Res = simplify(CondSimp, BO, Cond.Flag);
          if (!Res) {
            IsContra = true;
            return;
          }
        }
      }
      if (CondSimp.Cond) {
        Cond.Condition = CondSimp.Cond;
        Cond.Flag = CondSimp.Flag;
      }
    }
  }
}

void CondChainInfo::setCondStr(ASTContext *Context) {
  for (CondStatus &Cond : Chain) {
    if (Cond.Condition) {
      Cond.Condition->setCondStr(Context);
    }
  }
}

void CondChainInfo::findCallExprs() {
  OrderedSet<const CallExpr *> Set;
  for (const CFGBlock *Blk : Path) {
    for (const CFGElement &E : *Blk) {
      if (optional<CFGStmt> S = E.getAs<CFGStmt>()) {
        const Stmt *Stmt = S->getStmt();
        Set.merge(getCallExpr(Stmt));
      }
    }
  }
  if (!Set.empty()) {
    for (const CallExpr *CE : Set.orderedElements()) {
      const FunctionDecl *FD = CE->getDirectCallee();
      if (FD) {
        FuncCallInfo[FD].push_back({CE, {}});
      }
    }
  }
}

void CondChainInfo::traceBack() {
  unsigned CondNum = Chain.size();
  for (unsigned J = 0; J < CondNum; ++J) {
    CondStatus &Cond = Chain[J];
    if (Cond.Condition) {
      if (Cond.Condition->containDeclRefExpr()) {
        for (const DeclRefExpr *DRE : Cond.Condition->getDeclRefExprList()) {
          const Stmt *LastDefStmt = findLastDefStmt(DRE, J);
          if (LastDefStmt) {
            bool Exam = examineLastDef(DRE, LastDefStmt, Cond);
            if (Exam)
              Cond.LastDefStmts.insert(LastDefStmt);
            else {
              IsContra = true;
            }
          } else if (DRE->getDecl()->getKind() == Decl::Kind::ParmVar) {
            // handle ParmVar
            Cond.ParmVars.insert(cast<ParmVarDecl>(DRE->getDecl()));
          }
        }
      }
    }
  }
}

void CondChainInfo::findContra() {
  if (IsContra)
    return;
  unsigned CondNum = Chain.size();
  unordered_map<string, bool> CondMap;
  for (unsigned J = 0; J < CondNum; ++J) {
    CondStatus &Cond = Chain[J];
    if (Cond.Condition) {
      // handle call expression in the condition
      if (Cond.Condition->containCallExpr()) {
        for (const CallExpr *CE : Cond.Condition->getCallExprList()) {
          if (CE->getDirectCallee()) {
            IsContra = !setFuncCallInfo(Cond, CE);
            if (IsContra)
              return;
          }
        }
      }
      // handle last definition and call expression in the trace back
      for (const Stmt *S : Cond.LastDefStmts) {
        switch (S->getStmtClass()) {
        default:
          break;
        case Stmt::DeclStmtClass: {
          const DeclStmt *DS = cast<DeclStmt>(S);
          for (const Decl *D : DS->decls()) {
            if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
              if (VD->hasInit()) {
                const Expr *Init = VD->getInit()->IgnoreParenImpCasts();
                if (Init->getStmtClass() == Stmt::CXXMemberCallExprClass ||
                    Init->getStmtClass() == Stmt::CallExprClass) {
                  if (cast<CallExpr>(Init)->getDirectCallee()) {
                    IsContra = !setFuncCallInfo(Cond, cast<CallExpr>(Init));
                    if (IsContra)
                      return;
                  }
                } else {
                  IsContra = !setDefInfo(Cond, Init);
                  if (IsContra)
                    return;
                }
              }
            }
          }
          break;
        }
        case Stmt::BinaryOperatorClass: {
          const BinaryOperator *BO = cast<BinaryOperator>(S);
          if (BO->isAssignmentOp()) {
            Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
            if (RHS->getStmtClass() == Stmt::CXXMemberCallExprClass ||
                RHS->getStmtClass() == Stmt::CallExprClass) {
              if (cast<CallExpr>(RHS)->getDirectCallee()) {
                IsContra = !setFuncCallInfo(Cond, cast<CallExpr>(RHS));
                if (IsContra)
                  return;
              }
            } else {
              IsContra = !setDefInfo(Cond, RHS);
              if (IsContra)
                return;
            }
          }
          break;
        }
        }
      }
      // handle ParmVars
      for (const ParmVarDecl *PVD : Cond.ParmVars) {
        IsContra = !setParmInfo(Cond, PVD);
        if (IsContra)
          return;
      }
      // handle condition without definitions or call expressions or ParmVars
      if (!Cond.Condition->containCallExpr() && Cond.LastDefStmts.empty() &&
          Cond.ParmVars.empty()) {
        string CondStr = Cond.Condition->getCondStr();
        bool Flag = Cond.Flag;
        if (Cond.Condition->isNot())
          Flag = !Flag;
        if (CondMap.find(CondStr) == CondMap.end()) {
          CondMap[CondStr] = Flag;
        } else if (CondMap[CondStr] != Flag) {
          IsContra = true;
          return;
        }
      }
    }
  }
}

bool CondChainInfo::setFuncCallInfo(CondStatus &Cond, const CallExpr *CE) {
  const FunctionDecl *FuncDecl = CE->getDirectCallee();
  bool Flag = Cond.Flag;
  if (Cond.Condition->isNot())
    Flag = !Flag;
  if (FuncCallInfo.find(FuncDecl) == FuncCallInfo.end()) {
    errs() << "Function " << FuncDecl->getNameAsString()
           << " is not in FuncCallInfo\n";
  }
  vector<CallExprInfo> &CallExprInfoList = FuncCallInfo[FuncDecl];
  for (CallExprInfo &CallExprInfo : CallExprInfoList) {
    if (CallExprInfo.CE == CE) {
      if (CallExprInfo.CondInfos.find(Cond.Condition->getCondStr()) ==
          CallExprInfo.CondInfos.end()) {
        CallExprInfo.CondInfos[Cond.Condition->getCondStr()] = Flag;
      } else if (CallExprInfo.CondInfos[Cond.Condition->getCondStr()] != Flag) {
        return false;
      }
      break;
    }
  }
  return true;
}

bool CondChainInfo::setDefInfo(CondStatus &Cond, const Stmt *S) {
  LastDefInfo::DefInfoMap &DefInfoMap = DefInfo.DefInfo;
  bool Flag = Cond.Flag;
  if (Cond.Condition->isNot())
    Flag = !Flag;
  if (DefInfoMap.find(S) == DefInfoMap.end() ||
      DefInfoMap[S].find(Cond.Condition->getCondStr()) == DefInfoMap[S].end()) {
    DefInfoMap[S][Cond.Condition->getCondStr()] = Flag;
  } else if (DefInfoMap[S][Cond.Condition->getCondStr()] != Flag) {
    return false;
  }
  return true;
}

bool CondChainInfo::setParmInfo(CondStatus &Cond, const ParmVarDecl *PVD) {
  LastDefInfo::ParmInfoMap &ParmInfoMap = DefInfo.ParmInfo;
  bool Flag = Cond.Flag;
  if (Cond.Condition->isNot())
    Flag = !Flag;
  if (ParmInfoMap.find(PVD) == ParmInfoMap.end() ||
      ParmInfoMap[PVD].find(Cond.Condition->getCondStr()) ==
          ParmInfoMap[PVD].end()) {
    ParmInfoMap[PVD][Cond.Condition->getCondStr()] = Flag;
  } else if (ParmInfoMap[PVD][Cond.Condition->getCondStr()] != Flag) {
    return false;
  }
  return true;
}

const Stmt *CondChainInfo::findLastDefStmt(const DeclRefExpr *DeclRef,
                                           unsigned Loc) {
  bool Found = false;
  const Stmt *TraceBack = nullptr;
  for (auto It = Path.rend() - Loc; It != Path.rend() && !Found; ++It) {
    const CFGBlock *Blk = *It;
    const CFGElement *E = Blk->rbegin();
    if (Blk->getTerminator().isValid()) {
      if (Blk->size() <= 1)
        continue;
      E = E + 1;
    }
    for (; E != Blk->rend() && !Found; ++E) {
      if (optional<CFGStmt> S = E->getAs<CFGStmt>()) {
        const Stmt *Stmt = S->getStmt();
        switch (Stmt->getStmtClass()) {
        default:
          break;
        case Stmt::DeclStmtClass: {
          const DeclStmt *DS = cast<DeclStmt>(Stmt);
          for (const Decl *D : DS->decls()) {
            if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
              if (VD == DeclRef->getDecl() && VD->hasInit()) {
                Found = true;
                TraceBack = DS;
                break;
              }
            }
          }
          break;
        }
        case Stmt::BinaryOperatorClass: {
          const BinaryOperator *BO = cast<BinaryOperator>(Stmt);
          if (BO->isAssignmentOp()) {
            Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
            if (LHS->getStmtClass() == Stmt::DeclRefExprClass) {
              DeclRefExpr *DRE = cast<DeclRefExpr>(LHS);
              if (DRE->getDecl() == DeclRef->getDecl()) {
                Found = true;
                TraceBack = BO;
              }
            }
          }
          break;
        }
        case Stmt::UnaryOperatorClass: {
          const UnaryOperator *UO = cast<UnaryOperator>(Stmt);
          if (UO->isIncrementDecrementOp()) {
            Expr *SubExpr = UO->getSubExpr()->IgnoreParenImpCasts();
            if (SubExpr->getStmtClass() == Stmt::DeclRefExprClass) {
              DeclRefExpr *DRE = cast<DeclRefExpr>(SubExpr);
              if (DRE->getDecl() == DeclRef->getDecl()) {
                Found = true;
                TraceBack = UO;
              }
            }
          }
          break;
        }
        }
      }
    }
  }
  return TraceBack;
}

bool CondChainInfo::examineLastDef(const DeclRefExpr *DeclRef,
                                   const Stmt *LastDefStmt,
                                   CondStatus &CondStatus) {
  bool Res = true;
  switch (LastDefStmt->getStmtClass()) {
  default:
    break;
  case Stmt::DeclStmtClass: {
    const DeclStmt *DS = cast<DeclStmt>(LastDefStmt);
    for (const Decl *D : DS->decls()) {
      if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
        if (VD == DeclRef->getDecl()) {
          if (VD->hasInit()) {
            Res = checkLiteralExpr(VD->getInit()->IgnoreParenImpCasts(),
                                   CondStatus);
          }
          break;
        }
      }
    }
    break;
  }
  case Stmt::BinaryOperatorClass: {
    const BinaryOperator *BO = cast<BinaryOperator>(LastDefStmt);
    if (BO->getOpcode() == BinaryOperatorKind::BO_Assign) {
      Res = checkLiteralExpr(BO->getRHS()->IgnoreParenImpCasts(), CondStatus);
    }
    break;
  }
  }
  return Res;
}

void CondChainInfo::dumpFuncCallInfo() {
  for (auto &It : FuncCallInfo) {
    outs() << "Function: " << It.first->getNameAsString() << "\n";
    for (CallExprInfo &CEI : It.second) {
      outs() << "CallExpr: ";
      CEI.CE->dumpColor();
      outs() << "\n";
      for (auto &It : CEI.CondInfos) {
        outs() << "Condition: " << It.first << " Value: ";
        outs() << (It.second ? "True" : "False") << "\n";
      }
    }
  }
}

void CondChainInfo::dump(ASTContext *Context, unsigned Indent) {
  string IndentStr(Indent, ' ');
  outs() << IndentStr;
  unsigned CondNum = Chain.size();
  unsigned I = 0;
  for (unsigned J = 0; J < CondNum; ++J) {
    if (Chain[J].Condition) {
      if (I++ > 0)
        outs() << " \033[36m\033[1m->\033[0m ";
      Chain[J].dump(Context);
    }
  }
  outs() << "\n" + IndentStr;
  I = 0;
  for (const CFGBlock *Blk : Path) {
    if (I++ > 0)
      outs() << " \033[36m\033[1m->\033[0m ";
    outs() << Blk->getBlockID();
  }
  outs() << "\n";
}

json CondChainInfo::toTestReqs(ASTContext *Context) {
  json Json;
  Json["preconditions"] = json::array();
  Json["mock"] = json::object();

  set<CondStatus> CondStatusSet;
  for (CondStatus &Cond : Chain) {
    if (Cond.Condition) {
      if (CondStatusSet.find(Cond) == CondStatusSet.end()) {
        CondStatusSet.insert(Cond);
        bool Flag = Cond.Flag;
        if (Cond.Condition->isNot())
          Flag = !Flag;
        Json["preconditions"].push_back(
            {{"condition", Cond.Condition->getCondStr()},
             {"value", Flag},
             {"last_def", Cond.getLastDefStrVec(Context)}});
      }
    }
  }
  CondStatusSet.clear();

  vector<StringList> Actions;
  StringList CondList;
  for (auto &FuncCall : FuncCallInfo) {
    const FunctionDecl *FD = FuncCall.first;
    string FuncName = FD->getNameAsString();
    vector<CallExprInfo> &CallExprInfoList = FuncCall.second;
    for (CallExprInfo &CallExprInfo : CallExprInfoList) {
      for (auto &CondInfo : CallExprInfo.CondInfos) {
        string Condition = CondInfo.first;
        bool Flag = CondInfo.second;
        Condition += " is ";
        Condition += (Flag ? "true" : "false");
        CondList.push_back(Condition);
      }
      Actions.push_back(CondList);
      CondList.clear();
    }
    string ClassName = "";
    string ClassFile = "";
    if (FD->isCXXClassMember()) {
      const CXXRecordDecl *CRD = cast<CXXRecordDecl>(FD->getParent());
      ClassFile =
          Context->getSourceManager().getFilename(CRD->getLocation()).str();
      SmallVector<char, 128> RealPath;
      sys::fs::real_path(ClassFile, RealPath);
      ClassFile = string(RealPath.begin(), RealPath.end());
      if (ClassFile.find(RealProjectPath) == string::npos) {
        ClassFile = ClassFile.substr(ClassFile.find_last_of("/") + 1);
      }
      ClassName = CRD->getNameAsString();
    }
    string FileName =
        Context->getSourceManager().getFilename(FD->getLocation()).str();
    SmallVector<char, 128> RealPath;
    sys::fs::real_path(FileName, RealPath);
    string ResolvedPath(RealPath.begin(), RealPath.end());
    Json["mock"][ClassName]["file"] = ClassFile;
    Json["mock"][ClassName]["content"].push_back(
        {{"function", FuncName},
         {"virtual", FD->isVirtualAsWritten()},
         {"static", FD->isStatic()},
         {"user", ResolvedPath.find(RealProjectPath) != string::npos},
         {"actions", Actions},
         {"return", FD->getReturnType().getAsString()},
         {"return_ref", FD->getReturnType()->isReferenceType()}});
    Actions.clear();
  }

  return Json;
}

string CondChainInfo::getReturnStr(ASTContext *Context, string ReturnType) {
  string Result = "";
  string Str;
  raw_string_ostream OS(Str);
  const CFGBlock *Blk = Path[Path.size() - 2];
  if (!Blk->hasNoReturnElement()) {
    if (optional<CFGStmt> S = Blk->back().getAs<CFGStmt>()) {
      if (S->getStmt()->getStmtClass() == Stmt::ReturnStmtClass) {
        const ReturnStmt *RS = cast<ReturnStmt>(S->getStmt());
        RS->getRetValue()->printPretty(OS, nullptr,
                                       Context->getPrintingPolicy());
        OS.flush();
        Result += "a " + ReturnType + " value: " + Str;
      }
    }
  }
  return Result;
}

set<tuple<const Stmt *, string, bool>> CondChainInfo::getCondSet() const {
  set<tuple<const Stmt *, string, bool>> Set;
  for (CondStatus Cond : Chain) {
    if (Cond.Condition) {
      bool Flag = Cond.Flag;
      Set.insert(
          {Cond.Condition->getCond(), Cond.Condition->getCondStr(), Flag});
    }
  }
  return Set;
}

} // namespace BrInfo