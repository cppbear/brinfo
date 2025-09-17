#pragma once

#include "clang/Analysis/CFG.h"

using namespace clang;
using namespace llvm;
using namespace std;

namespace BrInfo {

inline void rtrim(string &S) {
  S.erase(find_if(S.rbegin(), S.rend(),
                  [](unsigned char Ch) { return !isspace(Ch) && Ch != ';'; })
              .base(),
          S.end());
}

class BaseCond {
public:
  enum CondKind { IF, CASE, DEFAULT, LOOP, TRY };

private:
  const CondKind Kind;

protected:
  const Stmt *Cond = nullptr;
  string CondStr;
  bool IsNot = false;
  bool ContainDeclRefExpr = false;
  bool ContainCallExpr = false;
  vector<const DeclRefExpr *> DeclRefExprList;
  vector<const CallExpr *> CallExprList;
  void findDeclRefExpr(const Stmt *S);
  void findCallExpr(const Stmt *S);

public:
  BaseCond(CondKind K, const Stmt *Cond) : Kind(K), Cond(Cond) {
    findDeclRefExpr(Cond);
    findCallExpr(Cond);
  }
  virtual ~BaseCond() { Cond = nullptr; }
  CondKind getKind() const { return Kind; }
  virtual void dump(const ASTContext *Context) = 0;
  virtual string toString(const ASTContext *Context) = 0;
  const Stmt *getCond() { return Cond; }
  string getCondStr() { return CondStr; }
  bool isNot() { return IsNot; }
  bool containDeclRefExpr() { return ContainDeclRefExpr; }
  bool containCallExpr() { return ContainCallExpr; }
  vector<const DeclRefExpr *> &getDeclRefExprList() { return DeclRefExprList; }
  vector<const CallExpr *> &getCallExprList() { return CallExprList; }
  virtual void setCondStr(const ASTContext *Context);
};

class IfCond : public BaseCond {
public:
  IfCond(const Stmt *Cond) : BaseCond(IF, Cond) {
  }
  virtual ~IfCond() {}
  void dump(const ASTContext *Context) override;
  string toString(const ASTContext *Context) override;
  static bool classof(const BaseCond *Cond) { return Cond->getKind() == IF; }
};

class CaseCond : public BaseCond {
  const Stmt *Case = nullptr;

public:
  CaseCond(const Stmt *Cond, const Stmt *Case)
      : BaseCond(CASE, Cond), Case(Case) {
  }
  virtual ~CaseCond() { Case = nullptr; }
  void dump(const ASTContext *Context) override;
  void setCondStr(const ASTContext *Context) override;
  string toString(const ASTContext *Context) override;
  static bool classof(const BaseCond *Cond) { return Cond->getKind() == CASE; }
};

class DefaultCond : public BaseCond {
  vector<const Stmt *> Cases;

public:
  DefaultCond(const Stmt *Cond, vector<const Stmt *> Cases)
      : BaseCond(DEFAULT, Cond), Cases(Cases) {
  }
  virtual ~DefaultCond() {}
  void dump(const ASTContext *Context) override;
  void setCondStr(const ASTContext *Context) override;
  string toString(const ASTContext *Context) override;
  static bool classof(const BaseCond *Cond) {
    return Cond->getKind() == DEFAULT;
  }
};

class LoopCond : public BaseCond {
public:
  LoopCond(const Stmt *Cond) : BaseCond(LOOP, Cond) {
  }
  virtual ~LoopCond() {}
  void dump(const ASTContext *Context) override;
  string toString(const ASTContext *Context) override;
  static bool classof(const BaseCond *Cond) { return Cond->getKind() == LOOP; }
};

class TryCond : public BaseCond {};

} // namespace BrInfo
