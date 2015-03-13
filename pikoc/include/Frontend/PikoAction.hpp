#ifndef PIKO_ACTION_HPP
#define PIKO_ACTION_HPP

#include "Frontend/PikoASTConsumer.hpp"
#include "PikoSummary.hpp"

#include "clang/Tooling/Tooling.h"

class PikoAction : public clang::ASTFrontendAction {
public:
	explicit PikoAction(
			PipeSummary *p
		, std::map<std::string, stageSummary>* s
		, int passNum)
		: psum_(p)
		, stageMap_(s)
		, passNum_(passNum)
	{}

	virtual clang::ASTConsumer *CreateASTConsumer(
		clang::CompilerInstance &ci, llvm::StringRef inFile)
	{
		return new PikoASTConsumer(ci, psum_, stageMap_, passNum_);
	}

private:
	PipeSummary* psum_;
	std::map<std::string, stageSummary>* stageMap_;
	int passNum_;
};

class PikoActionFactory : public clang::tooling::FrontendActionFactory {
public:
	PikoActionFactory(
			PipeSummary *p
		, std::map<std::string, stageSummary>* s
		, int passNum)
		: psum_(p)
		, stageMap_(s)
		, passNum_(passNum)
	{}

	virtual clang::FrontendAction* create() {
		return new PikoAction(psum_, stageMap_, passNum_);
	}

private:
	PipeSummary* psum_;
	std::map<std::string, stageSummary>* stageMap_;
	int passNum_;
};

#endif // PIKO_ACTION_HPP
