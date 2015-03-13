#ifndef CLANG_UTILITIES_HPP
#define CLANG_UTILITIES_HPP

#include <iostream>
#include <string>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Expr.h>

std::string getCalledFuncName(clang::CallExpr *f);
std::string getFuncName(clang::FunctionDecl *f);
std::string getRefName(clang::DeclRefExpr *f);
clang::Stmt* unrollCasts(clang::Stmt *s);
bool findFuncRecur(clang::Stmt *s, std::string name);
bool getSourceCode(clang::CXXMethodDecl *m, const clang::SourceManager &srcMgr,
	std::string &srcFileName, std::string &src);
int getTemplateArgInt(clang::TemplateArgument tmp, const clang::ASTContext& context,
	std::string msg);
void verifyNonNegative(int val, std::string msg);

#endif //CLANG_UTILITIES_HPP
