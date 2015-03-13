#ifndef STAGE_AST_VISITOR_HPP
#define STAGE_AST_VISITOR_HPP

#include "PikoSummary.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"

class StageASTVisitor : public clang::RecursiveASTVisitor<StageASTVisitor> {
public:
	explicit StageASTVisitor(
			const clang::CompilerInstance &ci
		, std::map<std::string, stageSummary> *m)
		: context(ci.getASTContext())
		, srcMgr(ci.getSourceManager())
		, stageMap(m)
		, nextTypeNum_(1)
	{}

	bool VisitCXXRecordDecl(clang::CXXRecordDecl *decl);

	static bool isPikoStage(clang::CXXRecordDecl *d);

private:
	clang::ASTContext &context;
	const clang::SourceManager &srcMgr;
	std::map<std::string, stageSummary> *stageMap;

	int nextTypeNum_;

	bool addAssignBinSummary(clang::CXXMethodDecl *m, stageSummary *ssum);
	bool addScheduleSummary(clang::CXXMethodDecl *m, stageSummary *ssum);
	bool addProcessSummary(clang::CXXMethodDecl *m, stageSummary *ssum);

	int getNextTypeNum();
};

#endif //STAGE_AST_VISITOR_HPP
