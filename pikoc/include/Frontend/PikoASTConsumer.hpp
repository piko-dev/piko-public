#ifndef PIKO_AST_CONSUMER_HPP
#define PIKO_AST_CONSUMER_HPP

#include "Frontend/PipeASTVisitor.hpp"
#include "Frontend/StageASTVisitor.hpp"
#include "PikoSummary.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/CompilerInstance.h"

class PikoASTConsumer : public clang::ASTConsumer {
public:
	explicit PikoASTConsumer(
			const clang::CompilerInstance &ci
		, PipeSummary *p
		, std::map<std::string, stageSummary>* stageMap
		, int passNum)
		: stageVisitor_(ci, stageMap)
		, pipeVisitor_(ci, p, stageMap)
		, passNum_(passNum)
	{}

	virtual bool HandleTopLevelDecl(clang::DeclGroupRef dr);

private:
	StageASTVisitor stageVisitor_;
	PipeASTVisitor pipeVisitor_;
	int passNum_;
};

#endif //PIKO_AST_CONSUMER_HPP
