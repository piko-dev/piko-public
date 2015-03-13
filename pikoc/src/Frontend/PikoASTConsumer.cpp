#include "Frontend/PikoASTConsumer.hpp"

bool PikoASTConsumer::HandleTopLevelDecl(clang::DeclGroupRef dr) {
	for(clang::DeclGroupRef::iterator b = dr.begin(), e = dr.end();
				b != e; ++b)
	{
		switch(passNum_)
		{
			default: break;
			case 1: stageVisitor_.TraverseDecl(*b); break;
			case 2: pipeVisitor_.TraverseDecl(*b); break;
		}
	}
	return true;
}
