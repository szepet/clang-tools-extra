//===--- InfiniteLoopCheck.cpp - clang-tidy -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "InfiniteLoopCheck.h"
#include "../utils/ExprSequence.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Analysis/CFG.h"

using namespace clang::ast_matchers;
using namespace clang::tidy::utils;

namespace clang {
namespace tidy {
namespace misc {

static internal::Matcher<Stmt> loopEndingStmt() {
  return stmt(anyOf(breakStmt(), returnStmt(), gotoStmt(), cxxThrowExpr()));
}

void InfiniteLoopCheck::registerMatchers(MatchFinder *Finder) {
  const auto loopCondition = []() {
    return allOf(hasCondition(expr().bind("condition")),
                 anyOf(hasAncestor(lambdaExpr().bind("containing-lambda")),
                       hasAncestor(functionDecl().bind("containing-func"))),
                 unless(hasBody(hasDescendant(loopEndingStmt()))));
  };

  Finder->addMatcher(
      stmt(anyOf(whileStmt(loopCondition()), doStmt(loopCondition()),
                 forStmt(loopCondition())))
          .bind("loop-stmt"),
      this);
}

static internal::Matcher<Stmt>
changeByOperator(internal::Matcher<Decl> VarNodeMatcher) {
  return anyOf(
      unaryOperator(anyOf(hasOperatorName("--"), hasOperatorName("++")),
                    hasUnaryOperand(ignoringParenImpCasts(
                        declRefExpr(to(varDecl(VarNodeMatcher)))))),
      binaryOperator(anyOf(hasOperatorName("="), hasOperatorName("+="),
                           hasOperatorName("/="), hasOperatorName("*="),
                           hasOperatorName("-="), hasOperatorName("%="),
                           hasOperatorName("&="), hasOperatorName("|="),
                           hasOperatorName("^="), hasOperatorName("<<="),
                           hasOperatorName(">>=")),
                     hasLHS(ignoringParenImpCasts(
                         declRefExpr(to(varDecl(VarNodeMatcher)))))));
}

static internal::Matcher<Stmt>
callByRef(internal::Matcher<Decl> VarNodeMatcher) {
  return callExpr(forEachArgumentWithParam(
      declRefExpr(to(varDecl(VarNodeMatcher))),
      parmVarDecl(hasType(references(qualType(unless(isConstQualified())))))));
}

static internal::Matcher<Stmt>
assignedToRef(internal::Matcher<Decl> VarNodeMatcher) {
  return declStmt(hasDescendant(varDecl(
      allOf(hasType(referenceType()),
            hasInitializer(anyOf(
                initListExpr(has(declRefExpr(to(varDecl(VarNodeMatcher))))),
                declRefExpr(to(varDecl(VarNodeMatcher)))))))));
}

static internal::Matcher<Stmt>
getAddrTo(internal::Matcher<Decl> VarNodeMatcher) {
  return unaryOperator(
      hasOperatorName("&"),
      hasUnaryOperand(declRefExpr(hasDeclaration(VarNodeMatcher))));
}

static internal::Matcher<Stmt> escapeStmt(const VarDecl *VD) {
  // Escaping is covered as address-of operators, pass-by-ref function calls and
  // reference initialization on the variable body.
  return stmt(anyOf(callByRef(equalsNode(VD)), getAddrTo(equalsNode(VD)),
                    assignedToRef(equalsNode(VD))));
}

static internal::Matcher<Stmt> potentiallyModifyVarStmt(const VarDecl *VD) {
  return anyOf(hasDescendant(stmt(
                   anyOf(changeByOperator(equalsNode(VD)), escapeStmt(VD)))),
               stmt(anyOf(changeByOperator(equalsNode(VD)), escapeStmt(VD))));
}

static std::unique_ptr<ExprSequence> createSequence(Stmt *FunctionBody,
                                                    ASTContext &ASTCtx) {
  CFG::BuildOptions Options;
  Options.AddImplicitDtors = true;
  Options.AddTemporaryDtors = true;
  std::unique_ptr<CFG> TheCFG =
      CFG::buildCFG(nullptr, FunctionBody, &ASTCtx, Options);
  if (!TheCFG)
    return std::unique_ptr<ExprSequence>{};

  return llvm::make_unique<ExprSequence>(TheCFG.get(), &ASTCtx);
}

void InfiniteLoopCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *ContainingLambda =
      Result.Nodes.getNodeAs<LambdaExpr>("containing-lambda");
  const auto *ContainingFunc =
      Result.Nodes.getNodeAs<FunctionDecl>("containing-func");
  const auto *Cond = Result.Nodes.getNodeAs<Expr>("condition");
  const auto *LoopStmt = Result.Nodes.getNodeAs<Stmt>("loop-stmt");
  auto &ASTCtx = *Result.Context;

  Stmt *FunctionBody = nullptr;
  if (ContainingLambda)
    FunctionBody = ContainingLambda->getBody();
  else if (ContainingFunc)
    FunctionBody = ContainingFunc->getBody();
  else
    return;

  const auto CondVarMatches =
      match(findAll(declRefExpr(to(varDecl().bind("condvar")))), *Cond, ASTCtx);

  // Skip the cases where there is no condition variables.
  if (CondVarMatches.empty())
    return;

  std::unique_ptr<ExprSequence> Sequence = createSequence(FunctionBody, ASTCtx);
  for (const auto &E : CondVarMatches) {
    const VarDecl *CondVar = E.getNodeAs<VarDecl>("condvar");

    // TODO: handle cases with non-integer condition variables
    if (!CondVar->getType().getTypePtr()->isIntegerType())
      return;

    // In case the loop potentially changes any of the condition variables we
    // assume the loop is not infinite.
    SmallVector<BoundNodes, 1> Match;
    // In case of for loop we check the increment stmt and the body for changes
    // (excluding the init stmt).
    if (const auto ForLoop = dyn_cast<ForStmt>(LoopStmt)) {
      if (ForLoop->getInc())
        Match = match(potentiallyModifyVarStmt(CondVar), *ForLoop->getInc(),
                      ASTCtx);
      if (Match.empty() && ForLoop->getBody())
        Match = match(potentiallyModifyVarStmt(CondVar), *ForLoop->getBody(),
                      ASTCtx);
    } else {
      // In cases of while and do-while we can match the whole loop.
      Match = match(potentiallyModifyVarStmt(CondVar), *LoopStmt, ASTCtx);
    }
    if (!Match.empty())
      return;

    // Skip the cases where any of the condition variables come from outside
    // of the function in order to avoid false positives.
    Match = match(stmt(hasDescendant(varDecl(equalsNode(CondVar)))),
                  *FunctionBody, ASTCtx);
    if (Match.empty())
      return;

    // When a condition variable is escaped before the loop we skip since we
    // have no precise pointer analysis and want to avoid false positives.
    Match = match(
        stmt(forEachDescendant(stmt(escapeStmt(CondVar)).bind("escStmt"))),
        *FunctionBody, ASTCtx);
    for (const auto &ES : Match) {
      if (Sequence->potentiallyAfter(LoopStmt, ES.getNodeAs<Stmt>("escStmt")))
        return;
    }
  }

  // Creating the string containing the name of condition variables.
  std::string CondVarNames = "";
  for (const auto &CVM : CondVarMatches) {
    CondVarNames += CVM.getNodeAs<VarDecl>("condvar")->getNameAsString() + ", ";
  }
  CondVarNames.resize(CondVarNames.size() - 2);

  diag(LoopStmt->getLocStart(),
       "%plural{1:The condition variable|:None of the condition variables}0 "
       "(%1) %plural{1:is not|:are}0 updated in the loop body")
      << (unsigned)CondVarMatches.size() << CondVarNames;
}

} // namespace misc
} // namespace tidy
} // namespace clang
