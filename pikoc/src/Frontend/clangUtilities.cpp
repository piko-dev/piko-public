#include "Frontend/clangUtilities.hpp"

#include <fstream>
#include <sstream>

#include "llvm/Support/raw_ostream.h"

std::string getCalledFuncName(clang::CallExpr *expr) {
	clang::CXXDependentScopeMemberExpr* depExpr =
		llvm::dyn_cast<clang::CXXDependentScopeMemberExpr>(expr->getCallee());

	std::string funcName =
		(depExpr)
		? depExpr->getMemberNameInfo().getAsString()
		: getFuncName(expr->getDirectCallee());

		return funcName;
}

std::string getFuncName(clang::FunctionDecl *f) {
	if(f == NULL) {
		std::cerr << "cannot get function declaration\n";
		return "";
	}
	else {
		clang::DeclarationName declName = f->getNameInfo().getName();
		return declName.getAsString();
	}
}

std::string getRefName(clang::DeclRefExpr *f) {
	if(f == NULL) {
		std::cerr << "cannot get function declaration\n";
		return "";
	}
	else {
		clang::DeclarationName declName = f->getNameInfo().getName();
		return declName.getAsString();
	}
}

clang::Stmt* unrollCasts(clang::Stmt *s) {
	clang::Stmt *tmp = s;
	while(llvm::isa<clang::CastExpr>(tmp)) {
		clang::StmtRange child = tmp->children();
		tmp = *child;
	}
	return tmp;
}

bool findFuncRecur(clang::Stmt *s, std::string name) {
	for(clang::StmtRange range = s->children(); range; ++range) {
		clang::Stmt *curStmt = (*range);
		if(curStmt == NULL) continue;
		if(llvm::isa<clang::CallExpr>(curStmt)) {
			clang::CallExpr *call = llvm::cast<clang::CallExpr>(curStmt);
			clang::FunctionDecl *f = call->getDirectCallee();
			if(getFuncName(f) == name) return true;
		}
			if(findFuncRecur(curStmt, name)) return true;
	}
	return false;
}

bool getSourceCode(clang::CXXMethodDecl *m, const clang::SourceManager &srcMgr,
										std::string &srcFileName, std::string &src) {
  std::string codeStartLineString;
	int codeStartLine;
  std::string codeEndLineString;
  int codeEndLine;

  clang::SourceRange assignBinSrc = m->getSourceRange();
  std::istringstream ss1(assignBinSrc.getBegin().printToString(srcMgr));
  std::getline(ss1, srcFileName, ':');
  std::getline(ss1, codeStartLineString, ':');
  std::istringstream(codeStartLineString) >> codeStartLine;

  std::istringstream ss2(assignBinSrc.getEnd().printToString(srcMgr));
  std::getline(ss2, codeEndLineString, ':');
  std::getline(ss2, codeEndLineString, ':');
  std::istringstream(codeEndLineString) >> codeEndLine;

  codeStartLine += 1;
  codeEndLine -= 1;

  if(codeStartLine > codeEndLine) {
    src = "";
		return true;
  }

	std::ifstream srcFile(srcFileName.c_str());
	if(!srcFile.is_open()) {
		llvm::errs() << "Unable to open source file " << srcFileName << "\n";
		return false;
	}

	std::string l;
	std::vector<std::string> lines;
	while(!srcFile.eof()) {
		getline(srcFile, l);
		lines.push_back(l);
	}
	srcFile.close();

	l = "";
	for(int i=codeStartLine-1; i < codeEndLine; ++i) {
		l += lines[i] + "\n";
	}

	src = l;

	return true;
}

int getTemplateArgInt(clang::TemplateArgument tmp, const clang::ASTContext& context,
											std::string msg) {
  llvm::APSInt tmpArgInt;
  if(!tmp.getAsExpr()->EvaluateAsInt(tmpArgInt, context)) {
    llvm::errs() << msg << "\n";
    exit(1);
  }

  return tmpArgInt.getSExtValue();
}

void verifyNonNegative(int val, std::string msg) {
  if(val < 0) {
    llvm::errs() << msg << "\n";
    exit(2);
  }
}
