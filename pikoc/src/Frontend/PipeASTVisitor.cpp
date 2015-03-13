#include "Frontend/clangUtilities.hpp"
#include "Frontend/PipeASTVisitor.hpp"
#include "Frontend/StageASTVisitor.hpp"

bool PipeASTVisitor::VisitCXXRecordDecl(clang::CXXRecordDecl *decl) {

	// Stop if errors have occurred in compilation
	if(context.getDiagnostics().hasErrorOccurred()) {
		llvm::errs() << "Compilation errors have occurred. "
		<< "Please resolve the above error messages.\n";
		exit(5);
		return false;
	}

	if(pipeFound)
		return true;

	if(!decl->hasDefinition())
		return true;

	clang::CXXRecordDecl *defn = decl->getDefinition();

	if(!isPikoPipe(defn))
  {
		return true;
  }

	pipeFound = true;
	psum->name = defn->getIdentifier()->getName();

	bool constState_type_found = false;
	bool mutableState_type_found = false;
	bool input_type_found = false;

	//Add Stages to Pipe
	for(clang::RecordDecl::field_iterator
			bf = defn->field_begin(), ef = defn->field_end(); bf != ef; ++bf)
	{
		std::string var_name = bf->getNameAsString();

		if(var_name == "constState_")
		{
			if(bf->getType()->isPointerType())
			{

				const clang::QualType& qt = bf->getType()->getPointeeType();

				psum->constState_type = qt.getAsString();

				printf("Found constant state type %s\n", psum->constState_type.c_str());

				constState_type_found = true;
			}
			else
			{
				__DEB;
			}
		}

		if(var_name == "mutableState_")
		{
			if(bf->getType()->isPointerType())
			{

				const clang::QualType& qt = bf->getType()->getPointeeType();

				psum->mutableState_type = qt.getAsString();

				printf("Found mutable state type %s\n", psum->mutableState_type.c_str());

				mutableState_type_found = true;
			}
			else
			{
				__DEB;
			}
		}

		if(!bf->getType()->isRecordType())
			continue;

		clang::CXXRecordDecl *fieldClass =  bf->getType()->getAsCXXRecordDecl();

		if(var_name == "h_input")
		{
			if(fieldClass->getNameAsString() == "PikoArray")
			{
				assert(llvm::isa<clang::TemplateSpecializationType>(bf->getType()));

				const clang::TemplateSpecializationType* temptype =
				llvm::cast<clang::TemplateSpecializationType>(bf->getType());

				assert(temptype->getNumArgs() == 1);

				psum->input_type =
					temptype->getArgs()[0].getAsType()->getAsCXXRecordDecl()->getNameAsString();

				printf("Found input type %s\n", psum->input_type.c_str());

				input_type_found = true;
			}
			else
			{
				__DEB;
			}
		}

		if(!StageASTVisitor::isPikoStage(fieldClass))
			continue;

		if(!fieldClass->hasDefinition())
		{
			llvm::errs() <<
				"No definition for stage " << fieldClass->getNameAsString() <<
				" used in pipe " << psum->name <<  "\n";
			return false;
		}

		clang::CXXRecordDecl *fieldClassDefn = fieldClass->getDefinition();

		if(!addStageToPipe(fieldClassDefn, *bf)) {
			llvm::errs() << "Unable to add stage \"" << bf->getNameAsString()
				<< "\" of type \"" << fieldClass->getNameAsString()
				<< "\" to pipe \"" << psum->name <<  "\"\n";
			return false;
		}

		printf("Added stage %s\n", bf->getNameAsString().c_str());
	}

	assert(constState_type_found && mutableState_type_found && input_type_found);

	//Find pikoConnect calls
	//for(CXXRecordDecl::ctor_iterator
	//		bc = defn->ctor_begin(), ec = defn->ctor_end(); bc != ec; ++bc) {
	//}
	clang::Stmt *ctorBody = defn->ctor_begin()->getBody();
	for(clang::Stmt::child_range child = ctorBody->children(); child; ++child)
	{
		//(*child)->dump();

		if(!llvm::isa<clang::CallExpr>(*child))
		{
			//printf("  exiting early\n");
			continue;
		}
		else
		{
		}

		clang::CallExpr *pikoConn = llvm::cast<clang::CallExpr>(*child);

		if(getFuncName(pikoConn->getDirectCallee()) != "pikoConnect")
		{
			continue;
		}
		else
		{
		}

		clang::MemberExpr *outStage =
			llvm::cast<clang::MemberExpr>(unrollCasts(pikoConn->getArg(0)));
		clang::MemberExpr *inStage =
			llvm::cast<clang::MemberExpr>(unrollCasts(pikoConn->getArg(1)));

		std::string outStageName = outStage->getMemberNameInfo().getAsString();
		std::string inStageName = inStage->getMemberNameInfo().getAsString();

		stageSummary *outStageSum = psum->findStageByName(outStageName);
		stageSummary *inStageSum = psum->findStageByName(inStageName);

		llvm::APSInt outPortInt;
		llvm::APSInt inPortInt;
		if(!pikoConn->getArg(2)->EvaluateAsInt(outPortInt, context)
				|| !pikoConn->getArg(3)->EvaluateAsInt(inPortInt, context))
		{
			llvm::errs() << "pikoConnect port number arguments must "
				<< "be compile-time constants\n";
			return false;
		}

		int outPortNum = outPortInt.getSExtValue();
		int inPortNum = inPortInt.getSExtValue();

		outStageSum->nextStageNames.push_back(inStageSum->name);
		outStageSum->nextStagesByPort[outPortNum].push_back(inStageSum);
		outStageSum->outPortTypes[outPortNum] = inStageSum->typeNumber;
	}

	psum->processLinks();

	return true;
}

// Must pass in definition
bool PipeASTVisitor::addStageToPipe(clang::CXXRecordDecl *d, clang::FieldDecl *field) {
	stageSummary ssum = (*stageMap)[d->getNameAsString()];

	ssum.name = field->getNameAsString();
	ssum.fullType = field->getType().getAsString();

	psum->stages.push_back(ssum);

	return true;
}

// Must pass in definition
bool PipeASTVisitor::isPikoPipe(clang::CXXRecordDecl *d) {
	for(clang::CXXRecordDecl::base_class_iterator
			bbc = d->bases_begin(), ebc = d->bases_end(); bbc != ebc; ++bbc)
	{
		if(bbc->getType().getAsString() == "class PikoPipe")
    {
			return true;
    }
	}
	return false;
}

int PipeASTVisitor::getPortNumber(std::string portName) {
	if(portName == "out0") return 0;
	else if(portName == "out1") return 1;
	else if(portName == "out2") return 2;
	else if(portName == "out3") return 3;
	else if(portName == "out4") return 4;
	else return -1;
}
