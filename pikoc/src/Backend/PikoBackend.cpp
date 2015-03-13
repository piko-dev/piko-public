#include "Backend/PikoBackend.hpp"

#include "clang/Basic/TargetInfo.h"
#include "clang/CodeGen/ModuleBuilder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Parse/ParseAST.h"

#include "llvm/LLVMContext.h"
#include "llvm/PassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/IPO.h"

bool PikoBackend::createLLVMModule()
{
  clang::CompilerInstance *CI = new clang::CompilerInstance();
  CI->createDiagnostics(0,0);

  std::vector<const char*> args;
  args.push_back("-xc++");
  args.push_back("-D__PIKOC__");
  args.push_back("-D__PIKOC_DEVICE__");
  args.push_back("-fno-exceptions");
  args.push_back("-I");
  args.push_back(pikocOptions.workingDir.c_str());
  args.push_back("-I");
  args.push_back(pikocOptions.clangResourceDir.c_str());
  args.push_back("-I");
  args.push_back(pikocOptions.pikoIncludeDir.c_str());
  for(int i = 0; i < pikocOptions.includeDirs.size(); ++i) {
    args.push_back("-I");
    args.push_back(pikocOptions.includeDirs[i].c_str());
  }
  args.push_back(pikocOptions.inFileName.c_str());

  llvm::ArrayRef<const char*> argList(args);
  llvm::IntrusiveRefCntPtr<clang::DiagnosticsEngine> diagnostics(&CI->getDiagnostics());
  clang::CompilerInvocation *compInvoke =
    clang::createInvocationFromCommandLine(argList, diagnostics);
  CI->setInvocation(compInvoke);

  clang::TargetOptions TO; 
  TO.Triple = this->getTargetTriple() + "-" + pikocOptions.osString;
  clang::TargetInfo* feTarget =
    clang::TargetInfo::CreateTargetInfo(CI->getDiagnostics(), TO);
  CI->setTarget(feTarget);
  CI->createFileManager();
  CI->createSourceManager(CI->getFileManager());
  CI->createPreprocessor();
  clang::Preprocessor &PP = CI->getPreprocessor();
  PP.getBuiltinInfo().InitializeBuiltins(PP.getIdentifierTable(),PP.getLangOpts());

  CI->createASTContext();

  const clang::FileEntry* inFile = CI->getFileManager().getFile(pikocOptions.inFileName);
  CI->getSourceManager().createMainFileID(inFile);
  CI->getDiagnosticClient().BeginSourceFile(CI->getLangOpts(), &CI->getPreprocessor());

  llvm::LLVMContext& ctx = llvm::getGlobalContext();
  clang::CodeGenerator* llvmCodeGen = clang::CreateLLVMCodeGen(
    CI->getDiagnostics(), "PikoPipe", CI->getCodeGenOpts(), ctx);

  clang::ParseAST(CI->getPreprocessor(), llvmCodeGen, CI->getASTContext());

  if(CI->getDiagnostics().hasErrorOccurred()) {
    return false;
  }

  this->module = llvmCodeGen->GetModule();

	return true;
}

bool PikoBackend::optimizeLLVMModule(int optLevel)
{
  llvm::PassManagerBuilder   passBuilder;
  llvm::PassManager          modPassMgr;
  llvm::FunctionPassManager  fnPassMgr(module);

  passBuilder.OptLevel = optLevel;

  if(pikocOptions.inlineDevice)
    passBuilder.Inliner = llvm::createAlwaysInlinerPass();

  passBuilder.populateFunctionPassManager(fnPassMgr);
  passBuilder.populateModulePassManager(modPassMgr);

  fnPassMgr.doInitialization();
  for(llvm::Module::iterator ii = module->begin(), ie = module->end();
      ii != ie; ++ii)
  {
    fnPassMgr.run(*ii);
  }

  fnPassMgr.doFinalization();

  modPassMgr.run(*module);

  return true;
}
