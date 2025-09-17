#include "clang/AST/Decl.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/DOTGraphTraits.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/GraphWriter.h"

using namespace clang;

/// Retrieve the type of the temporary object whose lifetime was
/// extended by a local reference with the given initializer.
static QualType getReferenceInitTemporaryType(const Expr *Init,
                                              bool *FoundMTE = nullptr) {
  while (true) {
    // Skip parentheses.
    Init = Init->IgnoreParens();

    // Skip through cleanups.
    if (const ExprWithCleanups *EWC = dyn_cast<ExprWithCleanups>(Init)) {
      Init = EWC->getSubExpr();
      continue;
    }

    // Skip through the temporary-materialization expression.
    if (const MaterializeTemporaryExpr *MTE =
            dyn_cast<MaterializeTemporaryExpr>(Init)) {
      Init = MTE->getSubExpr();
      if (FoundMTE)
        *FoundMTE = true;
      continue;
    }

    // Skip sub-object accesses into rvalues.
    SmallVector<const Expr *, 2> CommaLHSs;
    SmallVector<SubobjectAdjustment, 2> Adjustments;
    const Expr *SkippedInit =
        Init->skipRValueSubobjectAdjustments(CommaLHSs, Adjustments);
    if (SkippedInit != Init) {
      Init = SkippedInit;
      continue;
    }

    break;
  }

  return Init->getType();
}

class StmtPrinterHelper : public PrinterHelper {
  using StmtMapTy = llvm::DenseMap<const Stmt *, std::pair<unsigned, unsigned>>;
  using DeclMapTy = llvm::DenseMap<const Decl *, std::pair<unsigned, unsigned>>;

  StmtMapTy StmtMap;
  DeclMapTy DeclMap;
  signed currentBlock = 0;
  unsigned currStmt = 0;
  const LangOptions &LangOpts;

public:
  StmtPrinterHelper(const CFG *cfg, const LangOptions &LO) : LangOpts(LO) {
    if (!cfg)
      return;
    for (CFG::const_iterator I = cfg->begin(), E = cfg->end(); I != E; ++I) {
      unsigned j = 1;
      for (CFGBlock::const_iterator BI = (*I)->begin(), BEnd = (*I)->end();
           BI != BEnd; ++BI, ++j) {
        if (std::optional<CFGStmt> SE = BI->getAs<CFGStmt>()) {
          const Stmt *stmt = SE->getStmt();
          std::pair<unsigned, unsigned> P((*I)->getBlockID(), j);
          StmtMap[stmt] = P;

          switch (stmt->getStmtClass()) {
          case Stmt::DeclStmtClass:
            DeclMap[cast<DeclStmt>(stmt)->getSingleDecl()] = P;
            break;
          case Stmt::IfStmtClass: {
            const VarDecl *var = cast<IfStmt>(stmt)->getConditionVariable();
            if (var)
              DeclMap[var] = P;
            break;
          }
          case Stmt::ForStmtClass: {
            const VarDecl *var = cast<ForStmt>(stmt)->getConditionVariable();
            if (var)
              DeclMap[var] = P;
            break;
          }
          case Stmt::WhileStmtClass: {
            const VarDecl *var = cast<WhileStmt>(stmt)->getConditionVariable();
            if (var)
              DeclMap[var] = P;
            break;
          }
          case Stmt::SwitchStmtClass: {
            const VarDecl *var = cast<SwitchStmt>(stmt)->getConditionVariable();
            if (var)
              DeclMap[var] = P;
            break;
          }
          case Stmt::CXXCatchStmtClass: {
            const VarDecl *var = cast<CXXCatchStmt>(stmt)->getExceptionDecl();
            if (var)
              DeclMap[var] = P;
            break;
          }
          default:
            break;
          }
        }
      }
    }
  }

  ~StmtPrinterHelper() override = default;

  const LangOptions &getLangOpts() const { return LangOpts; }
  void setBlockID(signed i) { currentBlock = i; }
  void setStmtID(unsigned i) { currStmt = i; }

  bool handledStmt(Stmt *S, raw_ostream &OS) override {
    StmtMapTy::iterator I = StmtMap.find(S);

    if (I == StmtMap.end())
      return false;

    if (currentBlock >= 0 && I->second.first == (unsigned)currentBlock &&
        I->second.second == currStmt) {
      return false;
    }

    OS << "[B" << I->second.first << "." << I->second.second << "]";
    return true;
  }

  bool handleDecl(const Decl *D, raw_ostream &OS) {
    DeclMapTy::iterator I = DeclMap.find(D);

    if (I == DeclMap.end())
      return false;

    if (currentBlock >= 0 && I->second.first == (unsigned)currentBlock &&
        I->second.second == currStmt) {
      return false;
    }

    OS << "[B" << I->second.first << "." << I->second.second << "]";
    return true;
  }
};

class CFGBlockTerminatorPrint
    : public StmtVisitor<CFGBlockTerminatorPrint, void> {
  raw_ostream &OS;
  StmtPrinterHelper *Helper;
  PrintingPolicy Policy;

public:
  CFGBlockTerminatorPrint(raw_ostream &os, StmtPrinterHelper *helper,
                          const PrintingPolicy &Policy)
      : OS(os), Helper(helper), Policy(Policy) {
    this->Policy.IncludeNewlines = false;
  }

  void VisitIfStmt(IfStmt *I) {
    OS << "if ";
    if (Stmt *C = I->getCond())
      C->printPretty(OS, Helper, Policy);
  }

  // Default case.
  void VisitStmt(Stmt *Terminator) {
    Terminator->printPretty(OS, Helper, Policy);
  }

  void VisitDeclStmt(DeclStmt *DS) {
    VarDecl *VD = cast<VarDecl>(DS->getSingleDecl());
    OS << "static init " << VD->getName();
  }

  void VisitForStmt(ForStmt *F) {
    OS << "for (";
    if (F->getInit())
      OS << "...";
    OS << "; ";
    if (Stmt *C = F->getCond())
      C->printPretty(OS, Helper, Policy);
    OS << "; ";
    if (F->getInc())
      OS << "...";
    OS << ")";
  }

  void VisitWhileStmt(WhileStmt *W) {
    OS << "while ";
    if (Stmt *C = W->getCond())
      C->printPretty(OS, Helper, Policy);
  }

  void VisitDoStmt(DoStmt *D) {
    OS << "do ... while ";
    if (Stmt *C = D->getCond())
      C->printPretty(OS, Helper, Policy);
  }

  void VisitSwitchStmt(SwitchStmt *Terminator) {
    OS << "switch ";
    Terminator->getCond()->printPretty(OS, Helper, Policy);
  }

  void VisitCXXTryStmt(CXXTryStmt *) { OS << "try ..."; }

  void VisitObjCAtTryStmt(ObjCAtTryStmt *) { OS << "@try ..."; }

  void VisitSEHTryStmt(SEHTryStmt *CS) { OS << "__try ..."; }

  void VisitAbstractConditionalOperator(AbstractConditionalOperator *C) {
    if (Stmt *Cond = C->getCond())
      Cond->printPretty(OS, Helper, Policy);
    OS << " ? ... : ...";
  }

  void VisitChooseExpr(ChooseExpr *C) {
    OS << "__builtin_choose_expr( ";
    if (Stmt *Cond = C->getCond())
      Cond->printPretty(OS, Helper, Policy);
    OS << " )";
  }

  void VisitIndirectGotoStmt(IndirectGotoStmt *I) {
    OS << "goto *";
    if (Stmt *T = I->getTarget())
      T->printPretty(OS, Helper, Policy);
  }

  void VisitBinaryOperator(BinaryOperator *B) {
    if (!B->isLogicalOp()) {
      VisitExpr(B);
      return;
    }

    if (B->getLHS())
      B->getLHS()->printPretty(OS, Helper, Policy);

    switch (B->getOpcode()) {
    case BO_LOr:
      OS << " || ...";
      return;
    case BO_LAnd:
      OS << " && ...";
      return;
    default:
      llvm_unreachable("Invalid logical operator.");
    }
  }

  void VisitExpr(Expr *E) { E->printPretty(OS, Helper, Policy); }

public:
  void print(CFGTerminator T) {
    switch (T.getKind()) {
    case CFGTerminator::StmtBranch:
      Visit(T.getStmt());
      break;
    case CFGTerminator::TemporaryDtorsBranch:
      OS << "(Temp Dtor) ";
      Visit(T.getStmt());
      break;
    case CFGTerminator::VirtualBaseBranch:
      OS << "(See if most derived ctor has already initialized vbases)";
      break;
    }
  }
};

static void print_initializer(raw_ostream &OS, StmtPrinterHelper &Helper,
                              const CXXCtorInitializer *I) {
  if (I->isBaseInitializer())
    OS << I->getBaseClass()->getAsCXXRecordDecl()->getName();
  else if (I->isDelegatingInitializer())
    OS << I->getTypeSourceInfo()->getType()->getAsCXXRecordDecl()->getName();
  else
    OS << I->getAnyMember()->getName();
  OS << "(";
  if (Expr *IE = I->getInit())
    IE->printPretty(OS, &Helper, PrintingPolicy(Helper.getLangOpts()));
  OS << ")";

  if (I->isBaseInitializer())
    OS << " (Base initializer)";
  else if (I->isDelegatingInitializer())
    OS << " (Delegating initializer)";
  else
    OS << " (Member initializer)";
}

static void print_construction_context(raw_ostream &OS,
                                       StmtPrinterHelper &Helper,
                                       const ConstructionContext *CC) {
  SmallVector<const Stmt *, 3> Stmts;
  switch (CC->getKind()) {
  case ConstructionContext::SimpleConstructorInitializerKind: {
    OS << ", ";
    const auto *SICC =
        cast<SimpleConstructorInitializerConstructionContext>(CC);
    print_initializer(OS, Helper, SICC->getCXXCtorInitializer());
    return;
  }
  case ConstructionContext::CXX17ElidedCopyConstructorInitializerKind: {
    OS << ", ";
    const auto *CICC =
        cast<CXX17ElidedCopyConstructorInitializerConstructionContext>(CC);
    print_initializer(OS, Helper, CICC->getCXXCtorInitializer());
    Stmts.push_back(CICC->getCXXBindTemporaryExpr());
    break;
  }
  case ConstructionContext::SimpleVariableKind: {
    const auto *SDSCC = cast<SimpleVariableConstructionContext>(CC);
    Stmts.push_back(SDSCC->getDeclStmt());
    break;
  }
  case ConstructionContext::CXX17ElidedCopyVariableKind: {
    const auto *CDSCC = cast<CXX17ElidedCopyVariableConstructionContext>(CC);
    Stmts.push_back(CDSCC->getDeclStmt());
    Stmts.push_back(CDSCC->getCXXBindTemporaryExpr());
    break;
  }
  case ConstructionContext::NewAllocatedObjectKind: {
    const auto *NECC = cast<NewAllocatedObjectConstructionContext>(CC);
    Stmts.push_back(NECC->getCXXNewExpr());
    break;
  }
  case ConstructionContext::SimpleReturnedValueKind: {
    const auto *RSCC = cast<SimpleReturnedValueConstructionContext>(CC);
    Stmts.push_back(RSCC->getReturnStmt());
    break;
  }
  case ConstructionContext::CXX17ElidedCopyReturnedValueKind: {
    const auto *RSCC =
        cast<CXX17ElidedCopyReturnedValueConstructionContext>(CC);
    Stmts.push_back(RSCC->getReturnStmt());
    Stmts.push_back(RSCC->getCXXBindTemporaryExpr());
    break;
  }
  case ConstructionContext::SimpleTemporaryObjectKind: {
    const auto *TOCC = cast<SimpleTemporaryObjectConstructionContext>(CC);
    Stmts.push_back(TOCC->getCXXBindTemporaryExpr());
    Stmts.push_back(TOCC->getMaterializedTemporaryExpr());
    break;
  }
  case ConstructionContext::ElidedTemporaryObjectKind: {
    const auto *TOCC = cast<ElidedTemporaryObjectConstructionContext>(CC);
    Stmts.push_back(TOCC->getCXXBindTemporaryExpr());
    Stmts.push_back(TOCC->getMaterializedTemporaryExpr());
    Stmts.push_back(TOCC->getConstructorAfterElision());
    break;
  }
  case ConstructionContext::LambdaCaptureKind: {
    const auto *LCC = cast<LambdaCaptureConstructionContext>(CC);
    Helper.handledStmt(const_cast<LambdaExpr *>(LCC->getLambdaExpr()), OS);
    OS << "+" << LCC->getIndex();
    return;
  }
  case ConstructionContext::ArgumentKind: {
    const auto *ACC = cast<ArgumentConstructionContext>(CC);
    if (const Stmt *BTE = ACC->getCXXBindTemporaryExpr()) {
      OS << ", ";
      Helper.handledStmt(const_cast<Stmt *>(BTE), OS);
    }
    OS << ", ";
    Helper.handledStmt(const_cast<Expr *>(ACC->getCallLikeExpr()), OS);
    OS << "+" << ACC->getIndex();
    return;
  }
  }
  for (auto I : Stmts)
    if (I) {
      OS << ", ";
      Helper.handledStmt(const_cast<Stmt *>(I), OS);
    }
}

static void print_elem(raw_ostream &OS, StmtPrinterHelper &Helper,
                       const CFGElement &E) {
  switch (E.getKind()) {
  case CFGElement::Kind::Statement:
  case CFGElement::Kind::CXXRecordTypedCall:
  case CFGElement::Kind::Constructor: {
    CFGStmt CS = E.castAs<CFGStmt>();
    const Stmt *S = CS.getStmt();
    assert(S != nullptr && "Expecting non-null Stmt");

    // special printing for statement-expressions.
    if (const StmtExpr *SE = dyn_cast<StmtExpr>(S)) {
      const CompoundStmt *Sub = SE->getSubStmt();

      auto Children = Sub->children();
      if (Children.begin() != Children.end()) {
        OS << "({ ... ; ";
        Helper.handledStmt(*SE->getSubStmt()->body_rbegin(), OS);
        OS << " })\n";
        return;
      }
    }
    // special printing for comma expressions.
    if (const BinaryOperator *B = dyn_cast<BinaryOperator>(S)) {
      if (B->getOpcode() == BO_Comma) {
        OS << "... , ";
        Helper.handledStmt(B->getRHS(), OS);
        OS << '\n';
        return;
      }
    }
    S->printPretty(OS, &Helper, PrintingPolicy(Helper.getLangOpts()));

    if (auto VTC = E.getAs<CFGCXXRecordTypedCall>()) {
      if (isa<CXXOperatorCallExpr>(S))
        OS << " (OperatorCall)";
      OS << " (CXXRecordTypedCall";
      print_construction_context(OS, Helper, VTC->getConstructionContext());
      OS << ")";
    } else if (isa<CXXOperatorCallExpr>(S)) {
      OS << " (OperatorCall)";
    } else if (isa<CXXBindTemporaryExpr>(S)) {
      OS << " (BindTemporary)";
    } else if (const CXXConstructExpr *CCE = dyn_cast<CXXConstructExpr>(S)) {
      OS << " (CXXConstructExpr";
      if (std::optional<CFGConstructor> CE = E.getAs<CFGConstructor>()) {
        print_construction_context(OS, Helper, CE->getConstructionContext());
      }
      OS << ", " << CCE->getType() << ")";
    } else if (const CastExpr *CE = dyn_cast<CastExpr>(S)) {
      OS << " (" << CE->getStmtClassName() << ", " << CE->getCastKindName()
         << ", " << CE->getType() << ")";
    }

    // Expressions need a newline.
    if (isa<Expr>(S))
      OS << '\n';

    break;
  }

  case CFGElement::Kind::Initializer:
    print_initializer(OS, Helper, E.castAs<CFGInitializer>().getInitializer());
    OS << '\n';
    break;

  case CFGElement::Kind::AutomaticObjectDtor: {
    CFGAutomaticObjDtor DE = E.castAs<CFGAutomaticObjDtor>();
    const VarDecl *VD = DE.getVarDecl();
    Helper.handleDecl(VD, OS);

    QualType T = VD->getType();
    if (T->isReferenceType())
      T = getReferenceInitTemporaryType(VD->getInit(), nullptr);

    OS << ".~";
    T.getUnqualifiedType().print(OS, PrintingPolicy(Helper.getLangOpts()));
    OS << "() (Implicit destructor)\n";
    break;
  }

  case CFGElement::Kind::LifetimeEnds:
    Helper.handleDecl(E.castAs<CFGLifetimeEnds>().getVarDecl(), OS);
    OS << " (Lifetime ends)\n";
    break;

  case CFGElement::Kind::LoopExit:
    OS << E.castAs<CFGLoopExit>().getLoopStmt()->getStmtClassName()
       << " (LoopExit)\n";
    break;

  case CFGElement::Kind::ScopeBegin:
    OS << "CFGScopeBegin(";
    if (const VarDecl *VD = E.castAs<CFGScopeBegin>().getVarDecl())
      OS << VD->getQualifiedNameAsString();
    OS << ")\n";
    break;

  case CFGElement::Kind::ScopeEnd:
    OS << "CFGScopeEnd(";
    if (const VarDecl *VD = E.castAs<CFGScopeEnd>().getVarDecl())
      OS << VD->getQualifiedNameAsString();
    OS << ")\n";
    break;

  case CFGElement::Kind::NewAllocator:
    OS << "CFGNewAllocator(";
    if (const CXXNewExpr *AllocExpr =
            E.castAs<CFGNewAllocator>().getAllocatorExpr())
      AllocExpr->getType().print(OS, PrintingPolicy(Helper.getLangOpts()));
    OS << ")\n";
    break;

  case CFGElement::Kind::DeleteDtor: {
    CFGDeleteDtor DE = E.castAs<CFGDeleteDtor>();
    const CXXRecordDecl *RD = DE.getCXXRecordDecl();
    if (!RD)
      return;
    CXXDeleteExpr *DelExpr = const_cast<CXXDeleteExpr *>(DE.getDeleteExpr());
    Helper.handledStmt(cast<Stmt>(DelExpr->getArgument()), OS);
    OS << "->~" << RD->getName().str() << "()";
    OS << " (Implicit destructor)\n";
    break;
  }

  case CFGElement::Kind::BaseDtor: {
    const CXXBaseSpecifier *BS = E.castAs<CFGBaseDtor>().getBaseSpecifier();
    OS << "~" << BS->getType()->getAsCXXRecordDecl()->getName() << "()";
    OS << " (Base object destructor)\n";
    break;
  }

  case CFGElement::Kind::MemberDtor: {
    const FieldDecl *FD = E.castAs<CFGMemberDtor>().getFieldDecl();
    const Type *T = FD->getType()->getBaseElementTypeUnsafe();
    OS << "this->" << FD->getName();
    OS << ".~" << T->getAsCXXRecordDecl()->getName() << "()";
    OS << " (Member object destructor)\n";
    break;
  }

  case CFGElement::Kind::TemporaryDtor: {
    const CXXBindTemporaryExpr *BT =
        E.castAs<CFGTemporaryDtor>().getBindTemporaryExpr();
    OS << "~";
    BT->getType().print(OS, PrintingPolicy(Helper.getLangOpts()));
    OS << "() (Temporary object destructor)\n";
    break;
  }
  }
}

static void print_block(raw_ostream &OS, const CFG *cfg, const CFGBlock &B,
                        StmtPrinterHelper &Helper, bool print_edges,
                        bool ShowColors) {
  Helper.setBlockID(B.getBlockID());

  // Print the header.
  if (ShowColors)
    OS.changeColor(raw_ostream::YELLOW, true);

  OS << "\n [B" << B.getBlockID();

  if (&B == &cfg->getEntry())
    OS << " (ENTRY)]\n";
  else if (&B == &cfg->getExit())
    OS << " (EXIT)]\n";
  else if (&B == cfg->getIndirectGotoBlock())
    OS << " (INDIRECT GOTO DISPATCH)]\n";
  else if (B.hasNoReturnElement())
    OS << " (NORETURN)]\n";
  else
    OS << "]\n";

  if (ShowColors)
    OS.resetColor();

  // Print the label of this block.
  if (Stmt *Label = const_cast<Stmt *>(B.getLabel())) {
    if (print_edges)
      OS << "  ";

    if (LabelStmt *L = dyn_cast<LabelStmt>(Label))
      OS << L->getName();
    else if (CaseStmt *C = dyn_cast<CaseStmt>(Label)) {
      OS << "case ";
      if (const Expr *LHS = C->getLHS())
        LHS->printPretty(OS, &Helper, PrintingPolicy(Helper.getLangOpts()));
      if (const Expr *RHS = C->getRHS()) {
        OS << " ... ";
        RHS->printPretty(OS, &Helper, PrintingPolicy(Helper.getLangOpts()));
      }
    } else if (isa<DefaultStmt>(Label))
      OS << "default";
    else if (CXXCatchStmt *CS = dyn_cast<CXXCatchStmt>(Label)) {
      OS << "catch (";
      if (const VarDecl *ED = CS->getExceptionDecl())
        ED->print(OS, PrintingPolicy(Helper.getLangOpts()), 0);
      else
        OS << "...";
      OS << ")";
    } else if (ObjCAtCatchStmt *CS = dyn_cast<ObjCAtCatchStmt>(Label)) {
      OS << "@catch (";
      if (const VarDecl *PD = CS->getCatchParamDecl())
        PD->print(OS, PrintingPolicy(Helper.getLangOpts()), 0);
      else
        OS << "...";
      OS << ")";
    } else if (SEHExceptStmt *ES = dyn_cast<SEHExceptStmt>(Label)) {
      OS << "__except (";
      ES->getFilterExpr()->printPretty(OS, &Helper,
                                       PrintingPolicy(Helper.getLangOpts()), 0);
      OS << ")";
    } else
      llvm_unreachable("Invalid label statement in CFGBlock.");

    OS << ":\n";
  }

  // Iterate through the statements in the block and print them.
  unsigned j = 1;

  for (CFGBlock::const_iterator I = B.begin(), E = B.end(); I != E; ++I, ++j) {
    // Print the statement # in the basic block and the statement itself.
    if (print_edges)
      OS << " ";

    OS << llvm::format("%3d", j) << ": ";

    Helper.setStmtID(j);

    print_elem(OS, Helper, *I);
  }

  // Print the terminator of this block.
  if (B.getTerminator().isValid()) {
    if (ShowColors)
      OS.changeColor(raw_ostream::GREEN);

    OS << "   T: ";

    Helper.setBlockID(-1);

    PrintingPolicy PP(Helper.getLangOpts());
    CFGBlockTerminatorPrint TPrinter(OS, &Helper, PP);
    TPrinter.print(B.getTerminator());
    OS << '\n';

    if (ShowColors)
      OS.resetColor();
  }

  if (print_edges) {
    // Print the predecessors of this block.
    if (!B.pred_empty()) {
      const raw_ostream::Colors Color = raw_ostream::BLUE;
      if (ShowColors)
        OS.changeColor(Color);
      OS << "   Preds ";
      if (ShowColors)
        OS.resetColor();
      OS << '(' << B.pred_size() << "):";
      unsigned i = 0;

      if (ShowColors)
        OS.changeColor(Color);

      for (CFGBlock::const_pred_iterator I = B.pred_begin(), E = B.pred_end();
           I != E; ++I, ++i) {
        if (i % 10 == 8)
          OS << "\n     ";

        CFGBlock *B = *I;
        bool Reachable = true;
        if (!B) {
          Reachable = false;
          B = I->getPossiblyUnreachableBlock();
        }

        OS << " B" << B->getBlockID();
        if (!Reachable)
          OS << "(Unreachable)";
      }

      if (ShowColors)
        OS.resetColor();

      OS << '\n';
    }

    // Print the successors of this block.
    if (!B.succ_empty()) {
      const raw_ostream::Colors Color = raw_ostream::MAGENTA;
      if (ShowColors)
        OS.changeColor(Color);
      OS << "   Succs ";
      if (ShowColors)
        OS.resetColor();
      OS << '(' << B.succ_size() << "):";
      unsigned i = 0;

      if (ShowColors)
        OS.changeColor(Color);

      for (CFGBlock::const_succ_iterator I = B.succ_begin(), E = B.succ_end();
           I != E; ++I, ++i) {
        if (i % 10 == 8)
          OS << "\n    ";

        CFGBlock *B = *I;

        bool Reachable = true;
        if (!B) {
          Reachable = false;
          B = I->getPossiblyUnreachableBlock();
        }

        if (B) {
          OS << " B" << B->getBlockID();
          if (!Reachable)
            OS << "(Unreachable)";
        } else {
          OS << " NULL";
        }
      }

      if (ShowColors)
        OS.resetColor();
      OS << '\n';
    }
  }
}

static StmtPrinterHelper *GraphHelper;

inline void dumpCFGToDot(const CFG *cfg, const LangOptions &LO,
                  const std::string &DirPath, const std::string &GraphName,
                  const std::string &Title) {
  StmtPrinterHelper H(cfg, LO);
  GraphHelper = &H;
  std::string Filename = DirPath + "/" + GraphName + ".dot";
  llvm::dumpDotGraphToFile(cfg, Filename, Title);
  GraphHelper = nullptr;
}

namespace llvm {

template <> struct DOTGraphTraits<const CFG *> : public DefaultDOTGraphTraits {
  DOTGraphTraits(bool isSimple = false) : DefaultDOTGraphTraits(isSimple) {}

  static std::string getNodeLabel(const CFGBlock *Node, const CFG *Graph) {
    std::string OutSStr;
    llvm::raw_string_ostream Out(OutSStr);
    print_block(Out, Graph, *Node, *GraphHelper, false, false);
    std::string &OutStr = Out.str();

    if (OutStr[0] == '\n')
      OutStr.erase(OutStr.begin());

    // Process string output to make it nicer...
    for (unsigned i = 0; i != OutStr.length(); ++i)
      if (OutStr[i] == '\n') { // Left justify
        OutStr[i] = '\\';
        OutStr.insert(OutStr.begin() + i + 1, 'l');
      }

    return OutStr;
  }
};

} // namespace llvm
