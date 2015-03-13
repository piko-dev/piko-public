#ifndef PIPE_AST_VISITOR_HPP
#define PIPE_AST_VISITOR_HPP

#include "PikoSummary.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"

class PipeASTVisitor : public clang::RecursiveASTVisitor<PipeASTVisitor> {
public:
	explicit PipeASTVisitor(
			const clang::CompilerInstance &ci
		, PipeSummary *p
		, std::map<std::string, stageSummary> *m)
		: context(ci.getASTContext())
		, srcMgr(ci.getSourceManager())
		, stageMap(m)
		, psum(p)
		, pipeFound(false)
	{}

	bool VisitCXXRecordDecl(clang::CXXRecordDecl *d);

private:
	clang::ASTContext &context;
	const clang::SourceManager &srcMgr;
	std::map<std::string, stageSummary> *stageMap;

	PipeSummary *psum;
	bool pipeFound;

	bool addStageToPipe(clang::CXXRecordDecl *d, clang::FieldDecl *field);

	bool isPikoPipe(clang::CXXRecordDecl *d);
	int getPortNumber(std::string portName);
};

#endif //PIPE_AST_VISITOR_HPP
