//===--- UseAfterMoveCheck.cpp - clang-tidy -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "InfiniteLoopCheck.h"

#include "clang/Analysis/CFG.h"
#include "clang/Lex/Lexer.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "../utils/ExprSequence.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace misc {

void InfiniteLoopCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(forStmt(hasCondition(expr().bind("condition")),
                             anyOf(hasAncestor(
                                     lambdaExpr().bind("containing-lambda")),
                                   hasAncestor(functionDecl().bind(
                                           "containing-func"))))
                             .bind("for-stmt"),
                     this);
}

static internal::Matcher <Stmt>
changeIntBoundNode(internal::Matcher <Decl> VarNodeMatcher) {
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

static internal::Matcher <Stmt>
callByRef(internal::Matcher <Decl> VarNodeMatcher) {
  return callExpr(forEachArgumentWithParam(
          declRefExpr(to(varDecl(VarNodeMatcher))),
          parmVarDecl(
                  hasType(references(qualType(unless(isConstQualified())))))));
}

static internal::Matcher <Stmt>
assignedToRef(internal::Matcher <Decl> VarNodeMatcher) {
  return declStmt(hasDescendant(varDecl(
          allOf(hasType(referenceType()),
                hasInitializer(anyOf(
                        initListExpr(
                                has(declRefExpr(to(varDecl(VarNodeMatcher))))),
                        declRefExpr(to(varDecl(VarNodeMatcher)))))))));
}

static internal::Matcher <Stmt>
getAddrTo(internal::Matcher <Decl> VarNodeMatcher) {
  return unaryOperator(
          hasOperatorName("&"),
          hasUnaryOperand(declRefExpr(hasDeclaration(VarNodeMatcher))));
}

static internal::Matcher <Stmt> hasSuspiciousStmt(const VarDecl *VD) {
  return hasDescendant(stmt(
          anyOf(gotoStmt(), switchStmt(), returnStmt(), breakStmt(),
                  // Escaping and not known mutation of the loop counter is handled
                  // by exclusion of assigning and address-of operators and
                  // pass-by-ref function calls on the loop counter from the body.
                changeIntBoundNode(equalsNode(VD)),
                callByRef(equalsNode(VD)),
                getAddrTo(equalsNode(VD)),
                assignedToRef(equalsNode(VD)))));
}

void InfiniteLoopCheck::check(const MatchFinder::MatchResult &Result) {
  //const auto *ContainingLambda =
  //        Result.Nodes.getNodeAs<LambdaExpr>("containing-lambda");
  const auto *ContainingFunc =
          Result.Nodes.getNodeAs<FunctionDecl>("containing-func");
  const auto *Cond = Result.Nodes.getNodeAs<Expr>("condition");
  const auto *LoopStmt = Result.Nodes.getNodeAs<ForStmt>("for-stmt");
  auto& ASTCtx = *Result.Context;
  auto Match =
          match(findAll(declRefExpr(to(varDecl().bind("condvar")))), *Cond,
                ASTCtx);

  llvm::SmallPtrSet<const VarDecl *, 8> CondVars;
/*
  CFG::BuildOptions Options;
  Options.AddImplicitDtors = true;
  Options.AddTemporaryDtors = true;
  std::unique_ptr <CFG> TheCFG =
          CFG::buildCFG(nullptr, FunctionBody, ASTCtx, Options);
  if (!TheCFG)
    return false;

  Sequence.reset(new ExprSequence(TheCFG.get(), ASTCtx));
*/
  for (auto &E : Match) {
    CondVars.insert(E.getNodeAs<VarDecl>("condvar"));
    const VarDecl *CondVar = E.getNodeAs<VarDecl>("condvar");

    auto Match =
            match(hasSuspiciousStmt(CondVar), *LoopStmt, ASTCtx);
    if (!Match.empty())
      return;

    Match = match(decl(hasDescendant(varDecl(equalsNode(CondVar)))),
                  *ContainingFunc, ASTCtx);
    if (Match.empty())
      return;

    Match = match(stmt(anyOf(callByRef(equalsNode(CondVar)),
                             getAddrTo(equalsNode(CondVar)),
                             assignedToRef(equalsNode(CondVar)))),
                  *ContainingFunc, ASTCtx);
    if (!Match.empty())
      return;

  }

  diag(LoopStmt->getLocStart(), "infinite bitch");

}

} // namespace misc
} // namespace tidy
} // namespace clang
