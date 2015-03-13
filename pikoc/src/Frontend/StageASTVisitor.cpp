#include "Frontend/clangUtilities.hpp"
#include "Frontend/StageASTVisitor.hpp"

bool StageASTVisitor::VisitCXXRecordDecl(clang::CXXRecordDecl *decl) {

	// Stop if errors have occurred in compilation
	if(context.getDiagnostics().hasErrorOccurred()) {
		llvm::errs() << "Compilation errors have occurred. "
		<< "Please resolve the above error messages.\n";
		exit(5);
		return false;
	}

	if(!decl->hasDefinition())
		return true;

	clang::CXXRecordDecl *d = decl->getDefinition();

	if(!isPikoStage(d))
		return true;

	stageSummary ssum;
	ssum.type = d->getNameAsString();
	ssum.typeNumber = getNextTypeNum();

	// printf("%s --> %d\n", ssum.type.c_str(), ssum.typeNumber);

	clang::CXXBaseSpecifier* base;
	for(clang::CXXRecordDecl::base_class_iterator
			bbc = d->bases_begin(), ebc = d->bases_end(); bbc != ebc; ++bbc)
	{
		std::string baseName = bbc->getType().getAsString();
		if(baseName.substr(0, 5) == "Stage" && baseName.substr(5,1) == "<")
		{
			base = bbc;
			break;
		}
	}

	const clang::TemplateArgument *tmpArgs =
		base->getType()->getAs<clang::TemplateSpecializationType>()->getArgs();

	// get binSizeX
	std::string msg = "Could not get template argument binSizeX for stage " + ssum.name;
	int binx = getTemplateArgInt(tmpArgs[0], context, msg);
	msg = "Template argument binSizeX for stage " + ssum.name + " should be non-negative";
	verifyNonNegative(binx, msg);

	// get binSizeY
	msg = "Could not get template argument binSizeY for stage " + ssum.name;
	int biny = getTemplateArgInt(tmpArgs[1], context, msg);
	msg = "Template argument binSizeY for stage " + ssum.name + " should be non-negative";
	verifyNonNegative(biny, msg);

	ssum.binsize = vec2i(binx,biny);

	// get threadsPerTile
	msg = "Could not get template argument threadsPerTile for stage " + ssum.name;
	int numThreads = getTemplateArgInt(tmpArgs[2], context, msg);
	msg = "Template argument threadsPerTile for stage "
		+ ssum.name + " should be non-negative";
	verifyNonNegative(numThreads, msg);

	ssum.threadsPerTile = numThreads;

	// get in primitive type
	const clang::TemplateArgument primTypeIn = tmpArgs[3];
	std::istringstream ssIn(primTypeIn.getAsType().getAsString());
	std::getline(ssIn, ssum.primTypeIn, ' ');
	std::getline(ssIn, ssum.primTypeIn, ' ');

	// get out primitive type
	const clang::TemplateArgument primTypeOut = tmpArgs[4];
	std::istringstream ssOut(primTypeOut.getAsType().getAsString());
	std::getline(ssOut, ssum.primTypeOut, ' ');
	std::getline(ssOut, ssum.primTypeOut, ' ');

	// Find assignBin, schedule, and process methods and create summaries for them
	for(clang::CXXRecordDecl::method_iterator ii = d->method_begin(), ie = d->method_end();
			ii != ie; ++ii)
	{
		clang::CXXMethodDecl* method = *ii;
		if(method->hasBody())
		{
			if(method->getNameAsString() == "assignBin")
				if(!addAssignBinSummary(method, &ssum)) exit(4);
			if(method->getNameAsString() == "schedule")
				if(!addScheduleSummary(method, &ssum)) exit(4);
			if(method->getNameAsString() == "process")
				if(!addProcessSummary(method, &ssum)) exit(4);
		}
	}

	(*stageMap)[ssum.type] = ssum;

	return true;
}

//Must pass in definition (i.e. the decl that has the function body)
bool StageASTVisitor::addAssignBinSummary(clang::CXXMethodDecl *m, stageSummary *ssum) {
	bool policyFound = false;
	bool manualFound = false;
	int numChildren = 0;

	std::string stageType = m->getParent()->getNameAsString();

	assignBinSummary assignSum;

	//Get source code for this phase
	if(!getSourceCode(m, srcMgr, assignSum.codeFile, assignSum.sourceCode)) {
		llvm::errs() << "Unable to get assignBin source code for " << stageType << "\n";
		return false;
	}

	clang::Stmt* funcBody = m->getBody();

	//if assignBin is empty
	if(funcBody->children().empty()) {
		policyFound = true;
		assignSum.policy = assignEmpty;
		assignSum.trivial = true;
	}

	for(clang::Stmt::child_range range = funcBody->children(); range; range++)
	{
		numChildren += 1;
		if(!llvm::isa<clang::CallExpr>(*range))
			continue;

		clang::CallExpr* expr = llvm::cast<clang::CallExpr>(*range);

		if(getCalledFuncName(expr) != "assignToBin")
			continue;

		clang::Expr* arg = expr->getArg(1);

		//if we found a policy
		if(arg->getType().getAsString() == "enum AssignPolicy")
		{
			//Make sure we haven't previously found a policy
			if(policyFound) {
				llvm::errs() << "Multiple assignBin policies stated for "
					<< stageType << ". Please use only one policy per stage.\n";
				return false;
			}

			policyFound = true;
			clang::ValueDecl* value = llvm::cast<clang::DeclRefExpr>(arg)->getDecl();
			clang::EnumConstantDecl* enumDecl = llvm::cast<clang::EnumConstantDecl>(value);
			std::string assignPolicy = enumDecl->getDeclName().getAsString();

			if(assignPolicy == "PREVIOUS_BINS")
				assignSum.policy = assignInBin;
			else if(assignPolicy == "BOUNDING_BOX")
				assignSum.policy = assignBoundingBox;
			else if(assignPolicy == "POSITION")
				assignSum.policy = assignPosition;
			else {
				llvm::errs() << "AssignBin policy not recognized for "
					<< stageType << ".\n";
				return false;
			}
		}
		//otherwise it is a manual binning call
		else
		{
			manualFound = true;
		}
	}

	//error if manual bins specified and policy specified
	if(manualFound && policyFound) {
		llvm::errs() << "AssignBin policy and manual binning specified for " << stageType
			<< ". Please use only one of these options.\n";
		return false;
	}

	//assignBin is trivial if the only thing in it is a policy
	if(policyFound && numChildren == 1)
		assignSum.trivial = true;
	if(manualFound)
		assignSum.policy = assignCustom;
	if(!manualFound && !policyFound) {
		assignSum.policy = assignEmpty;
	}

	//vector<stageSummary*> ssums = psum->findStageByType(stageType);
	//for(int i = 0;  i < ssums.size(); ++i)
		//ssums[i]->assignBin = assignSum;
	ssum->assignBin = assignSum;

	return true;
}

static bool getTileSplitSize(clang::ASTContext &context, scheduleSummary &schedSum,
							 clang::CallExpr *expr, std::string stageType) {
	clang::Expr* tileSplitExpr = expr->getArg(1);
	llvm::APSInt tileSplitSizeAPS;
	if(tileSplitExpr->isIntegerConstantExpr(tileSplitSizeAPS, context)) {
		int tileSplitSizeInt = tileSplitSizeAPS.getSExtValue();
		std::string msg = "tileSplitSize for stage " + stageType
			+ " must be non-negative";
		verifyNonNegative(tileSplitSizeInt, msg);
		schedSum.tileSplitSize = tileSplitSizeInt;
		return true;
	}

	llvm::errs() << "tileSplitSize for stage " << stageType
		<< " must be constant.\n";
	return false;
}
	
//Must pass in definition (i.e. the decl that has the function body)
bool StageASTVisitor::addScheduleSummary(clang::CXXMethodDecl *m, stageSummary *ssum) {
	bool schedPolicyFound = false;
	bool schedCustomFound = false;
	bool waitPolicyFound = false;
	bool waitCustomFound = false;
	int numChildren = 0;

	std::string stageType = m->getParent()->getNameAsString();

	scheduleSummary schedSum;

	//Get source code for this phase
	if(!getSourceCode(m, srcMgr, schedSum.codeFile, schedSum.sourceCode)) {
		llvm::errs() << "Unable to get assignBin source code for " << stageType << "\n";
		return false;
	}

	clang::Stmt* funcBody = m->getBody();

	for(clang::Stmt::child_range range = funcBody->children(); range; range++)
	{
		numChildren += 1;
		if(!llvm::isa<clang::CallExpr>(*range))
			continue;

		clang::CallExpr* expr = llvm::cast<clang::CallExpr>(*range);
		//if it is specifySchedule
		if(getCalledFuncName(expr) == "specifySchedule") {
			clang::Expr* arg = expr->getArg(0);
			//if we found a policy
			if(arg->getType().getAsString() == "enum SchedulePolicy") {
				//Make sure we haven't previously found a policy
				if(schedPolicyFound) {
					llvm::errs() << "Multiple schedule policies stated for "
						<< stageType << ". Please use only one policy per stage.\n";
					return false;
				}
				schedPolicyFound = true;
				clang::ValueDecl* value = llvm::cast<clang::DeclRefExpr>(arg)->getDecl();
				clang::EnumConstantDecl* enumDecl = llvm::cast<clang::EnumConstantDecl>(value);
				std::string schedulePolicy = enumDecl->getDeclName().getAsString();
				if(schedulePolicy == "LOAD_BALANCE") {
					schedSum.schedPolicy = schedLoadBalance;
					getTileSplitSize(context, schedSum, expr, stageType);
				}
				else if(schedulePolicy == "ROUND_ROBIN")
					schedSum.schedPolicy = schedDirectMap;
				else if(schedulePolicy == "SERIAL")
					schedSum.schedPolicy = schedSerialize;
				else if(schedulePolicy == "ALL") {
					schedSum.schedPolicy = schedAll;
					getTileSplitSize(context, schedSum, expr, stageType);
				}
				else {
					llvm::errs() << "Schedule policy not recognized for "
						<< stageType << ".\n";
					return false;
				}
			}
			//otherwise it is a custom schedule call
			else
				schedCustomFound = true;
		}
		// if it is specifyWait
		else if(getCalledFuncName(expr) == "specifyWait") {
			clang::Expr* arg = expr->getArg(0);
			//if we found a policy
			if(arg->getType().getAsString() == "enum WaitPolicy") {
				//Make sure we haven't previously found a policy
				if(waitPolicyFound) {
					llvm::errs() << "Multiple wait policies stated for "
						<< stageType << ". Please use only one policy per stage.\n";
					return false;
				}
				waitPolicyFound = true;
				clang::ValueDecl* value = llvm::cast<clang::DeclRefExpr>(arg)->getDecl();
				clang::EnumConstantDecl* enumDecl = llvm::cast<clang::EnumConstantDecl>(value);
				std::string waitPolicy = enumDecl->getDeclName().getAsString();
				if(waitPolicy == "BATCH") {
					schedSum.waitPolicy = waitBatch;
					clang::Expr* waitBatchExpr = expr->getArg(1);
					llvm::APSInt waitBatchSizeAPS;
					if(waitBatchExpr->isIntegerConstantExpr(waitBatchSizeAPS, context)) {
						int waitBatchSizeInt = waitBatchSizeAPS.getSExtValue();
						if(waitBatchSizeInt <= 0) {
							llvm::errs() << "Please specify a positive waitBatchSize for "
								<< stageType << ".\n";
							return false;
						}
						schedSum.waitBatchSize = waitBatchSizeInt;
					}
					else {
						llvm::errs() << "waitBatchSize for stage " << stageType
							<< " must be constant.\n";
						return false;
					}
				}
				else if(waitPolicy == "END_STAGE")
					schedSum.waitPolicy = waitEndStage;
				else if(waitPolicy == "END_BIN")
					schedSum.waitPolicy = waitEndBin;
				else {
					llvm::errs() << "Wait policy not recognized for "
						<< stageType << ".\n";
					return false;
				}
			}
			//otherwise it is a custom wait
			else
				waitCustomFound = true;
		}
	}
	//Schedule Policy
	//error if no schedule found
	if(!schedCustomFound && !schedPolicyFound) {
		llvm::errs() << "No schedule found for " << stageType
			<< ". Please specify either a schedule policy or a custom schedule.\n";
		return false;
	}
	//error if custom schedule specified and policy specified
	if(schedCustomFound && schedPolicyFound) {
		llvm::errs() << "Schedule policy and custom schedule specified for " << stageType
			<< ". Please use only one of these options.\n";
		return false;
	}
	if(schedCustomFound)
		schedSum.schedPolicy = schedCustom;

	//Wait Policy
	//if no wait found, set waitNone
	else if(!waitCustomFound && !waitPolicyFound) {
		schedSum.waitPolicy = waitNone;
	}
	//error if custom schedule specified and policy specified
	else if(waitCustomFound && waitPolicyFound) {
		llvm::errs() << "Wait policy and custom wait specified for " << stageType
			<< ". Please use only one of these options.\n";
		return false;
	}
	else if(waitCustomFound)
		schedSum.waitPolicy = waitCustom;

	//schedule is trivial if the only thing in it is a schedule policy
	//or schedule and wait policies
	if((schedPolicyFound && numChildren == 1)
			|| (schedPolicyFound && waitPolicyFound && numChildren == 2))
		schedSum.trivial = true;
	
	//vector<stageSummary*> ssums = psum->findStageByType(stageType);
	//for(int i = 0;  i < ssums.size(); ++i)
		//ssums[i]->schedules[0] = schedSum;
	ssum->schedules[0] = schedSum;
	
	return true;
}

//Must pass in definition (i.e. the decl that has the function body)
bool StageASTVisitor::addProcessSummary(clang::CXXMethodDecl *m, stageSummary *ssum) {
	bool policyFound = false;
	bool maxOutPrimsFound = false;
	int roughNumEmits = 0;
	int numChildren = 0;

	std::string stageType = m->getParent()->getNameAsString();

	processSummary processSum;

	//Get source code for this phase
	if(!getSourceCode(m, srcMgr, processSum.codeFile, processSum.sourceCode)) {
		llvm::errs() << "Unable to get assignBin source code for " << stageType << "\n";
		return false;
	}

	clang::Stmt* funcBody = m->getBody();

	//if process is empty
	if(funcBody->children().empty()) {
		policyFound = true;
		processSum.maxOutPrims = 0;
		processSum.policy = procEmpty;
		processSum.trivial = true;
	}

	for(clang::Stmt::child_range range = funcBody->children(); range; range++) {
		numChildren += 1;
		if(llvm::isa<clang::CallExpr>(*range)) {
			clang::CallExpr* expr = llvm::cast<clang::CallExpr>(*range);
			//if we found maxOutPrims
			if(getCalledFuncName(expr) == "specifyMaxOutPrims") {
				//Make sure we haven't previous found maxOutPrims
				if(maxOutPrimsFound) {
						llvm::errs() << "Multiple maxOutPrims numbers stated for " << stageType
							<< ". Please only use specifyMaxOutPrims once per stage.\n";
						return false;
				}
				maxOutPrimsFound = true;

				clang::Expr* arg = expr->getArg(0);
				llvm::APSInt maxOutPrimsAPS;
				if(arg->isIntegerConstantExpr(maxOutPrimsAPS, context)) {
					int maxOutPrimsInt = maxOutPrimsAPS.getSExtValue();
					if(maxOutPrimsInt <= 0) {
						llvm::errs() << "Please specify a positive maxOutPrims for "
							<< stageType << ".\n";
						return false;
					}
					processSum.maxOutPrims = maxOutPrimsInt;
				}
				else {
					llvm::errs() << "maxOutPrims for stage " << stageType
						<< " must be constant.\n";
					return false;
				}
			}
		}
	}
	//if policy not specified, set policy to custom
	if(!policyFound) {
		processSum.policy = procCustom;
	}

	//process is trivial (and empty) if the only thing in it is specifyMaxOutPrims
	if(numChildren == 1 && maxOutPrimsFound) {
		processSum.trivial = true;
		processSum.policy = procEmpty;
	}

	//vector<stageSummary*> ssums = psum->findStageByType(stageType);
	//for(int i = 0;  i < ssums.size(); ++i)
		//ssums[i]->process = processSum;
	ssum->process = processSum;

	return true;
}

// Must pass in definition
bool StageASTVisitor::isPikoStage(clang::CXXRecordDecl *d) {
	for(clang::CXXRecordDecl::base_class_iterator
			bbc = d->bases_begin(), ebc = d->bases_end(); bbc != ebc; ++bbc)
	{
		std::string baseName = bbc->getType().getAsString();
		if(baseName.substr(0, 5) == "Stage" && baseName.substr(5,1) == "<")
		{
			return true;
		}
		else
		{ }
	}

	return false;
}

int StageASTVisitor::getNextTypeNum() {
	int ret = nextTypeNum_;
	nextTypeNum_ += 1;
	return ret;
}
