#include "Condition.h"

namespace BrInfo {

void BaseCond::findDeclRefExpr(const Stmt *S) {
  switch (S->getStmtClass()) {
  default:
    break;
  case Stmt::DeclRefExprClass: {
    const DeclRefExpr *DeclRef = cast<DeclRefExpr>(S);
    Decl::Kind Kind = DeclRef->getDecl()->getKind();
    if (Kind == Decl::Kind::Var) {
      const VarDecl *Var = cast<VarDecl>(DeclRef->getDecl());
      if (Var->isLocalVarDecl()) {
        DeclRefExprList.push_back(DeclRef);
        ContainDeclRefExpr = true;
      }
    } else if (Kind == Decl::Kind::ParmVar) {
      DeclRefExprList.push_back(DeclRef);
      ContainDeclRefExpr = true;
    }
    break;
  }
  case Stmt::BinaryOperatorClass: {
    const BinaryOperator *BO = cast<BinaryOperator>(S);
    findDeclRefExpr(BO->getLHS()->IgnoreParenImpCasts());
    findDeclRefExpr(BO->getRHS()->IgnoreParenImpCasts());
    break;
  }
  case Stmt::UnaryOperatorClass: {
    const UnaryOperator *UO = cast<UnaryOperator>(S);
    findDeclRefExpr(UO->getSubExpr()->IgnoreParenImpCasts());
    break;
  }
  }
}

void BaseCond::findCallExpr(const Stmt *S) {
  switch (S->getStmtClass()) {
  default:
    break;
  case Stmt::CXXMemberCallExprClass: {
  }
    LLVM_FALLTHROUGH;
  case Stmt::CallExprClass: {
    const CallExpr *Call = cast<CallExpr>(S);
    CallExprList.push_back(Call);
    ContainCallExpr = true;
    break;
  }
  case Stmt::BinaryOperatorClass: {
    const BinaryOperator *BO = cast<BinaryOperator>(S);
    findCallExpr(BO->getLHS()->IgnoreParenImpCasts());
    findCallExpr(BO->getRHS()->IgnoreParenImpCasts());
    break;
  }
  case Stmt::UnaryOperatorClass: {
    const UnaryOperator *UO = cast<UnaryOperator>(S);
    findCallExpr(UO->getSubExpr()->IgnoreParenImpCasts());
    break;
  }
  }
}

void BaseCond::setCondStr(const ASTContext *Context) {
  if (!CondStr.empty())
    return;
  llvm::raw_string_ostream OS(CondStr);
  if (Cond->getStmtClass() == Stmt::BinaryOperatorClass &&
      cast<BinaryOperator>(Cond)->getOpcode() == BinaryOperatorKind::BO_NE) {
    IsNot = true;
    Expr *LHS = cast<BinaryOperator>(Cond)->getLHS()->IgnoreParenImpCasts();
    Expr *RHS = cast<BinaryOperator>(Cond)->getRHS()->IgnoreParenImpCasts();
    LHS->printPretty(OS, nullptr, Context->getPrintingPolicy());
    OS << " == ";
    RHS->printPretty(OS, nullptr, Context->getPrintingPolicy());
  } else if (Cond->getStmtClass() == Stmt::UnaryOperatorClass &&
             cast<UnaryOperator>(Cond)->getOpcode() ==
                 UnaryOperatorKind::UO_LNot) {
    IsNot = true;
    Expr *SubExpr =
        cast<UnaryOperator>(Cond)->getSubExpr()->IgnoreParenImpCasts();
    SubExpr->printPretty(OS, nullptr, Context->getPrintingPolicy());
  } else {
    Cond->printPretty(OS, nullptr, Context->getPrintingPolicy());
  }
  rtrim(CondStr);
}

void IfCond::dump(const ASTContext *Context) { Cond->dumpPretty(*Context); }

string IfCond::toString(const ASTContext *Context) {
  string Str;
  llvm::raw_string_ostream OS(Str);
  Cond->printPretty(OS, nullptr, Context->getPrintingPolicy());
  rtrim(Str);
  return Str;
}

void LoopCond::dump(const ASTContext *Context) { Cond->dumpPretty(*Context); }

string LoopCond::toString(const ASTContext *Context) {
  string Str;
  llvm::raw_string_ostream OS(Str);
  Cond->printPretty(OS, nullptr, Context->getPrintingPolicy());
  return OS.str();
}

void CaseCond::setCondStr(const ASTContext *Context) {
  if (!CondStr.empty())
    return;
  llvm::raw_string_ostream OS(CondStr);
  if (Cond->getStmtClass() == Stmt::BinaryOperatorClass &&
      cast<BinaryOperator>(Cond)->getOpcode() == BinaryOperatorKind::BO_NE) {
    IsNot = true;
    Expr *LHS = cast<BinaryOperator>(Cond)->getLHS()->IgnoreParenImpCasts();
    Expr *RHS = cast<BinaryOperator>(Cond)->getRHS()->IgnoreParenImpCasts();
    LHS->printPretty(OS, nullptr, Context->getPrintingPolicy());
    OS << " == ";
    RHS->printPretty(OS, nullptr, Context->getPrintingPolicy());
  } else if (Cond->getStmtClass() == Stmt::UnaryOperatorClass &&
             cast<UnaryOperator>(Cond)->getOpcode() ==
                 UnaryOperatorKind::UO_LNot) {
    IsNot = true;
    Expr *SubExpr =
        cast<UnaryOperator>(Cond)->getSubExpr()->IgnoreParenImpCasts();
    SubExpr->printPretty(OS, nullptr, Context->getPrintingPolicy());
  } else {
    Cond->printPretty(OS, nullptr, Context->getPrintingPolicy());
  }
  rtrim(CondStr);
  OS << " == ";
  Case->printPretty(OS, nullptr, Context->getPrintingPolicy());
  rtrim(CondStr);
}

void CaseCond::dump(const ASTContext *Context) {
  Cond->dumpPretty(*Context);
  outs() << ": ";
  Case->dumpPretty(*Context);
}

string CaseCond::toString(const ASTContext *Context) {
  string Str;
  llvm::raw_string_ostream OS(Str);
  Cond->printPretty(OS, nullptr, Context->getPrintingPolicy());
  OS << ": ";
  Case->printPretty(OS, nullptr, Context->getPrintingPolicy());
  return OS.str();
}

void DefaultCond::setCondStr(const ASTContext *Context) {
  if (!CondStr.empty())
    return;
  llvm::raw_string_ostream OS(CondStr);
  if (Cond->getStmtClass() == Stmt::BinaryOperatorClass &&
      cast<BinaryOperator>(Cond)->getOpcode() == BinaryOperatorKind::BO_NE) {
    IsNot = true;
    Expr *LHS = cast<BinaryOperator>(Cond)->getLHS()->IgnoreParenImpCasts();
    Expr *RHS = cast<BinaryOperator>(Cond)->getRHS()->IgnoreParenImpCasts();
    LHS->printPretty(OS, nullptr, Context->getPrintingPolicy());
    OS << " == ";
    RHS->printPretty(OS, nullptr, Context->getPrintingPolicy());
  } else if (Cond->getStmtClass() == Stmt::UnaryOperatorClass &&
             cast<UnaryOperator>(Cond)->getOpcode() ==
                 UnaryOperatorKind::UO_LNot) {
    IsNot = true;
    Expr *SubExpr =
        cast<UnaryOperator>(Cond)->getSubExpr()->IgnoreParenImpCasts();
    SubExpr->printPretty(OS, nullptr, Context->getPrintingPolicy());
  } else {
    Cond->printPretty(OS, nullptr, Context->getPrintingPolicy());
  }
  rtrim(CondStr);
  string SwitchValue = CondStr;
  unsigned I = 0;
  for (const Stmt *Case : Cases) {
    if (I++ > 0) {
      OS << " || " << SwitchValue;
    }
    OS << " == ";
    Case->printPretty(OS, nullptr, Context->getPrintingPolicy());
  }
  rtrim(CondStr);
}

void DefaultCond::dump(const ASTContext *Context) {
  Cond->dumpPretty(*Context);
  outs() << ": ";
  for (auto *Case : Cases) {
    Case->dumpPretty(*Context);
    outs() << " ";
  }
}

string DefaultCond::toString(const ASTContext *Context) {
  string Str;
  llvm::raw_string_ostream OS(Str);
  Cond->printPretty(OS, nullptr, Context->getPrintingPolicy());
  OS << ": ";
  for (auto *Case : Cases) {
    Case->printPretty(OS, nullptr, Context->getPrintingPolicy());
    OS << " ";
  }
  return OS.str();
}

} // namespace BrInfo
