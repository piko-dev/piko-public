#include "Backend/PTXBackend.hpp"

#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/TypeBuilder.h"
#include "llvm/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/IPO.h"

#include <string>

bool PTXBackend::emitDefines(std::ostream& outfile) {
  outfile << "#define __PIKOC_PTX__\n\n";

  outfile << "// device-side members\n";
  outfile << "#define __PIKO_DEVICE_MEMBERS__ \\\n";
  outfile << "  CUdevice    cuDevice; \\\n";
  outfile << "  CUcontext   cuContext; \\\n";
  outfile << "  CUmodule    cuModule; \\\n";
  outfile << "  CUdeviceptr d_pikoScreen; \\\n";
  outfile << "  CUdeviceptr d_mutableState; \\\n";
  outfile << "  CUdeviceptr d_input; \\\n";

  for(std::vector<stageSummary>::iterator
      ii = psum.stages.begin(), ie = psum.stages.end();
      ii != ie; ii++)
  {
    std::string stgName = ii->name;
    std::string stgType = ii->fullType;
    outfile << "  CUdeviceptr d_" << stgName << "; \\\n";
  }

  // outfile << "  CUdeviceptr d_vertexShader;     \\\n";
  // outfile << "  CUdeviceptr d_raster;           \\\n";

  int curKernel = 0;
  outfile << "  CUfunction kernel" << curKernel++ << "; \\\n";
  for(std::vector< std::vector<stageSummary*> >::iterator
      ii = kernelList.begin(), ie = kernelList.end();
      ii != ie; ++ii)
  {
    stageSummary* stg = (*ii)[0];
    if(!pikocOptions.optimize || !stg->schedules[0].trivial)
      outfile << "  CUfunction kernel" << curKernel++ << "; \\\n";
    outfile << "  CUfunction kernel" << curKernel++ << "; \\\n";
  }
  outfile << "  ;\n\n";

  // outfile << "  CUfunction kernel0;             \\\n";
  // outfile << "  CUfunction kernel1;             \\\n";
  // outfile << "  CUfunction kernel2;             \n";

  return true;
}

// This function is currently unused because we are relying on libNVVM
// for optimizations
bool PTXBackend::optimizeLLVMModule(int optLevel)
{
	return true;
}

bool PTXBackend::emitRunFunc(std::ostream& outfile)
{
  std::string tabs = "";
  std::string pipeName = psum.name;
  bool optimize = pikocOptions.optimize;

  outfile << "#ifdef __PIKOC_HOST__\n";
  outfile << "#ifndef PIKO_" << pipeName << "_RUNFUNC_H\n";
  outfile << "#define PIKO_" << pipeName << "_RUNFUNC_H\n";
  outfile << "\n";
  outfile << "#include <cuda.h>\n";
  outfile << "#include <builtin_types.h>\n";
  outfile << "#include <GL/glut.h>\n";
  outfile << "\n";

  outfile << "#include \"internal/cudaMacros.h\"\n";
  outfile << "#include <ctime>\n";
  outfile << "\n";

  outfile << "unsigned* pixelData;\n";
  outfile << "ConstantState* constStateInternal;\n";
  outfile << "\n";

  outfile << "void pikoDisplayFunc() {\n";
  outfile << "  //glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);\n";
  outfile << "  glDrawPixels(constStateInternal->screenSizeX, constStateInternal->screenSizeY,\n";
  outfile << "    GL_RGBA, GL_UNSIGNED_BYTE, pixelData);\n";
  outfile << "  glutSwapBuffers();\n";
  outfile << "}\n";
  outfile << "\n";

  outfile << "void " << pipeName << "::run (" 
    << psum.constState_type << "& h_constState, " << psum.mutableState_type << "& h_mutableState, "
    << psum.input_type << "* inputData, int count) {\n";

  if(pikocOptions.enableTimers) {
    outfile << "  clock_t totalTime, setupTime, kernelTime;\n";
    outfile << "  totalTime = clock();\n";
    outfile << "  setupTime = clock();\n";
    outfile << "\n";
  }

  outfile << "  h_constState.isConstantState();\n";
  outfile << "  h_mutableState.isMutableState();\n";
  outfile << "  inputData[0].isPrim();\n";
  outfile << "  constStateInternal = &h_constState;\n";
  outfile << "\n";
  outfile << "// CUDA driver setup\n";
  outfile << "  cuInit(0);\n";
  outfile << "  int deviceCount = 0;\n";
  outfile << "  cuDeviceGetCount(&deviceCount);\n";
  outfile << "  if(deviceCount == 0) {\n";
  outfile << "    std::cerr << \"There is no device supporting CUDA.\\n\";\n";
  outfile << "    return;\n";
  outfile << "  }\n";
  outfile << "\n";
  outfile << "  CUdevice cuDevice;\n";
  outfile << "  cuDeviceGet(&cuDevice, 0); \n";
  outfile << "\n";
  outfile << "  CUcontext cuContext;\n";
  outfile << "  cuCtxCreate(&cuContext, 0, cuDevice);\n";
  outfile << "\n";
  outfile << "  CUmodule cuModule;\n";
  outfile << "  CUDACHECK(cuModuleLoad(&cuModule, \"__pikoCompiledPipe.ptx\"));\n";
  outfile << "\n";
  outfile << "// Increase the GPU stack size\n";
  outfile << "  CUDACHECK(cuCtxSetLimit(CU_LIMIT_STACK_SIZE, " << GPU_STACK_SIZE << "))\n";
  outfile << "\n";
  outfile << "// Create a device pointer for each stage\n";
  outfile << "// and map the host pointer to the device pointer\n";
  outfile << "  std::map<void*, CUdeviceptr> stgMap;\n";

  for(std::vector<stageSummary>::iterator
      ii = psum.stages.begin(), ie = psum.stages.end();
      ii != ie; ii++)
  {
    std::string stgName = ii->name;
    std::string stgType = ii->fullType;
    outfile << "  CUdeviceptr d_" << stgName << ";\n";
    outfile << "  CUDACHECK(cuMemAlloc(&d_" << stgName
      << ", sizeof(" << stgType << ")));\n";
    outfile << "  stgMap[&" << stgName << "] = d_" << stgName << ";\n";
    outfile << "\n";
  }

  outfile << "// Create an instance of PikoScreen, call its allocate method,\n";
  outfile << "// and add it to the stage map\n";
  outfile << "  PikoScreen pikoScreen;\n";
  outfile << "  pikoScreen.allocate(&h_constState);\n";
  outfile << "  CUdeviceptr d_pikoScreen;\n";
  outfile << "  CUDACHECK(cuMemAlloc(&d_pikoScreen, sizeof(PikoScreen)));\n";
  outfile << "  CUDACHECK(cuMemcpyHtoD(d_pikoScreen, &pikoScreen, sizeof(pikoScreen)));\n";
  outfile << "  stgMap[&pikoScreen] = d_pikoScreen;\n";
  outfile << "\n";
  outfile << "// Make pikoScreen the output of any drain stages\n";
  for(std::vector<stageSummary*>::iterator
      ii = psum.drainStages.begin(), ie = psum.drainStages.end();
      ii != ie; ++ii)
  {
    stageSummary* stg = *ii;
    outfile << "  " << stg->name << ".outPort[0] = &pikoScreen;\n";
  }
  outfile << "\n";

  outfile << "// Piko initial input data\n";
  outfile << "  int numBlocks;\n";
  outfile << "  int numThreads;\n";
  outfile << "  CUdeviceptr d_mutableState;\n";
  outfile << "  CUdeviceptr d_input;\n";
  outfile << "  PikoArray<" << psum.input_type << "> h_input;\n";
  outfile << "\n";
  outfile << "  h_input.allocate();\n";
  outfile << "  h_input.copyData(inputData, count);\n";
  outfile << "  CUDACHECK(cuMemAlloc(&d_mutableState, sizeof(" << psum.mutableState_type << ")));\n";
  outfile << "  CUDACHECK(cuMemAlloc(&d_input, sizeof(" << psum.input_type << ")));\n";
  outfile << "  CUDACHECK(cuMemcpyHtoD(d_input, &h_input, sizeof(" << psum.input_type << ")));\n";
  outfile << "\n";

  outfile << "// Setup stages\n";
  for(std::vector<stageSummary*>::iterator
      ii = psum.stagesInOrder.begin(), ie = psum.stagesInOrder.end();
      ii != ie; ++ii)
  {
    stageSummary* stg = *ii;
    std::string stgName = stg->name;
    std::string stgType = stg->fullType;

    outfile << "  " << stgName << ".allocate(&h_constState, d_mutableState, stgMap,";
    if(stg->fusedWithNext)
      outfile << "true";
    else
      outfile << "false";
    outfile << ");\n";
    for(int i=0; i < NUM_PORTS; ++i) {
      outfile << "  " << stgName << ".outPortTypes[" << i << "] = "
        << stg->outPortTypes[i] << ";\n";
    }

    outfile << "  CUDACHECK(cuMemcpyHtoD(d_" << stgName << ", &" << stgName << ",\n"
            << "    sizeof(" << stgType << ")));\n";
  }
  outfile << "\n";

  int curKernel = 0;

  writeKernelFunctionFetch(curKernel, true, outfile);
  curKernel += 1;

  for(std::vector< std::vector<stageSummary*> >::iterator
      ii = kernelList.begin(), ie = kernelList.end();
      ii != ie; ++ii)
  {
    stageSummary* stg = (*ii)[0];

    if(!optimize || !stg->schedules[0].trivial) {
      writeKernelFunctionFetch(curKernel, true, outfile);
      curKernel += 1;
    }

    writeKernelFunctionFetch(curKernel, true, outfile);
    curKernel += 1;
  }

  if(pikocOptions.enableTimers) {
    outfile << "  setupTime = clock() - setupTime;\n";
    outfile << "\n";
  }

	tabs = "  ";

  // first run to clean the GPU
  // copy fresh state to device
  outfile << tabs << "// -----------------------\n";
  outfile << tabs << "// ------ FIRST RUN ------\n";
  outfile << tabs << "// -----------------------\n";
  outfile << tabs << "{\n";
	tabs = "    ";

  outfile << tabs << "CUdeviceptr d_constState;\n";
  outfile << tabs << "size_t      constStateSize;\n";
  outfile << tabs << "CUDACHECK(cuModuleGetGlobal(&d_constState, &constStateSize, cuModule, \"constState\"));\n";
  outfile << tabs << "CUDACHECK(cuMemcpyHtoD(d_constState, &h_constState, sizeof("
    << psum.constState_type << ")));\n";
  outfile << tabs << "CUDACHECK(cuMemcpyHtoD(d_mutableState, &h_mutableState, sizeof("
    << psum.mutableState_type << ")));\n";
	outfile << "\n";
  writeKernelCalls(tabs, outfile);
  outfile << tabs << "CUDACHECK(cuCtxSynchronize());\n";

	tabs = "  ";
  outfile << tabs << "}\n";
	outfile << "\n";

  outfile << tabs << "// -----------------------\n";
  outfile << tabs << "// ------ TIMED RUNS -----\n";
  outfile << tabs << "// -----------------------\n";
  if(pikocOptions.enableTimers) {
    outfile << tabs << "kernelTime = clock();\n";
  }
  // loop starts here
  outfile << tabs << "for(int i = 0; i < " << pikocOptions.numRuns << "; ++i)\n";
	outfile << tabs << "{\n";
	tabs = "    ";

  // copy fresh state to device
  outfile << tabs << "CUdeviceptr d_constState;\n";
  outfile << tabs << "size_t      constStateSize;\n";
  outfile << tabs << "CUDACHECK(cuModuleGetGlobal(&d_constState, &constStateSize, cuModule, \"constState\"));\n";
  outfile << tabs << "CUDACHECK(cuMemcpyHtoD(d_mutableState, &h_mutableState, sizeof("
    << psum.mutableState_type << ")));\n";
  outfile << tabs << "CUDACHECK(cuMemcpyHtoD(d_constState, &h_constState, sizeof("
    << psum.constState_type << ")));\n";
	outfile << "\n";
  writeKernelCalls(tabs, outfile);
  outfile << tabs << "CUDACHECK(cuCtxSynchronize());\n";

	tabs = "  ";
  outfile << tabs << "}\n";

  if(pikocOptions.enableTimers) {
    outfile << tabs << "kernelTime = clock() - kernelTime;\n";
  }

	outfile << "\n";
  outfile << "// Get Output\n";
  outfile << "  pixelData = pikoScreen.getData();\n";
  outfile << "\n";

  outfile << "// Free stages and input\n";
  outfile << "  " << "h_input.free();\n";
  outfile << "  " << "CUDACHECK(cuMemFree(d_input));\n";
  outfile << "\n";

  for(std::vector<stageSummary*>::iterator
      ii = psum.stagesInOrder.begin(), ie = psum.stagesInOrder.end();
      ii != ie; ++ii)
  {
      outfile << "  " << (*ii)->name << ".free();\n";
      outfile << "  CUDACHECK(cuMemFree(d_" << (*ii)->name << "));\n";
  }
  outfile << "\n";
/*
  outfile << "  int numPixels = pikoScreen.getNumPrims();\n";
  outfile << "  printf(\"Number of pixels: %d\\n\", numPixels);\n";
  outfile << "  for(int i = 0; i < numPixels; ++i) {\n";
  outfile << "    printf(\"  Position: (%u, %u)\\n\",\n";
  outfile << "      output[i].pos.x, output[i].pos.y);\n";
  outfile << "    printf(\"    Color (r, g, b): (%u, %u, %u)\\n\",\n";
  outfile << "      output[i].r, output[i].g, output[i].b);\n";
  outfile << "  }\n";
  outfile << "\n";
  outfile << "  free(output);\n";
*/
  outfile << "  pikoScreen.free();\n";
  outfile << "  CUDACHECK(cuMemFree(d_pikoScreen));\n";
  outfile << "  CUDACHECK(cuMemFree(d_mutableState));\n";
  outfile << "  CUDACHECK(cuCtxDestroy(cuContext));\n";
  outfile << "\n";

  if(pikocOptions.enableTimers) {
    outfile << "  totalTime = clock() - totalTime;\n";
    outfile << "  printf(\"\\n\");\n";
    outfile << "  printf(\"Number of timed pipeline runs: " << pikocOptions.numRuns
            << "\\n\");\n";
    outfile << "  printf(\"Setup Time        (msec): %f\\n\", "
            << "((float) setupTime * 1000.0f) / (float)CLOCKS_PER_SEC);\n";
		if(pikocOptions.numRuns == 0) {
			outfile << "  printf(\"Avg Kernel Time   (msec): No timed runs\\n\");\n";
		}
		else {
			outfile << "  printf(\"Avg Kernel Time   (msec): %f\\n\", "
				<< "((float) kernelTime * 1000.0f) / (float)CLOCKS_PER_SEC / " << pikocOptions.numRuns << ".0f );\n";
		}
    outfile << "  printf(\"Total Time        (msec): %f\\n\", "
            << "((float) totalTime * 1000.0f) / (float)CLOCKS_PER_SEC);\n";
		outfile << "\n";
  }

  outfile << "  glutDisplayFunc(pikoDisplayFunc);\n";
  outfile << "  glClearColor(0.0, 0.0, 0.0, 1.0);\n";
  outfile << "  glutMainLoop();\n";

  outfile << "}\n";
  outfile << "\n";

  outfile << "#endif // PIKO_" << pipeName << "_RUNFUNC_H\n";
  outfile << "#endif // __PIKOC_HOST__\n\n";


	return true;
}

bool PTXBackend::emitDeviceCode(std::string filename)
{
  std::string outFileName = filename + ".ptx";
  std::string kernelPrefix = "kernel";
  std::string atomicPrefix = "__atomic_";

  // NVVM intrinsic functions
  llvm::Function* threadIdx_x = llvm::Function::Create(
    llvm::TypeBuilder<llvm::types::i<32>(), true>::get(module->getContext()),
    llvm::GlobalValue::ExternalLinkage, "llvm.nvvm.read.ptx.sreg.tid.x", module);

  llvm::Function* blockIdx_x = llvm::Function::Create(
    llvm::TypeBuilder<llvm::types::i<32>(), true>::get(module->getContext()),
    llvm::GlobalValue::ExternalLinkage, "llvm.nvvm.read.ptx.sreg.ctaid.x", module);

  llvm::Function* blockDim_x = llvm::Function::Create(
    llvm::TypeBuilder<llvm::types::i<32>(), true>::get(module->getContext()),
    llvm::GlobalValue::ExternalLinkage, "llvm.nvvm.read.ptx.sreg.ntid.x", module);

  // NVVM atomic intrinsic functions
  llvm::Function* atomicAddFloat = llvm::Function::Create(
    llvm::TypeBuilder<llvm::types::ieee_float(
    llvm::types::ieee_float*, llvm::types::ieee_float),
    true>::get(module->getContext()),
    llvm::GlobalValue::ExternalLinkage, "llvm.nvvm.atomic.load.add.f32.p0f32", module);

  // NVVM named metadata (for specifying kernels)
  llvm::NamedMDNode* nvvmMD =
    module->getOrInsertNamedMetadata("nvvm.annotations");

  for(llvm::Module::iterator
      ii = module->begin(), ie = module->end(); ii != ie; ++ii)
  {
    std::string name = ii->getName();

    // Put in calls to intrinsic functions
    if(name == "threadIdx_x") {
      ii->replaceAllUsesWith(threadIdx_x);
    }
    else if(name == "blockIdx_x") {
      ii->replaceAllUsesWith(blockIdx_x);
    }
    else if(name == "blockDim_x") {
      ii->replaceAllUsesWith(blockDim_x);
    }
    else if(name == "__atomic_nvvm_addFloat__") {
      ii->replaceAllUsesWith(atomicAddFloat);
    }
    // Delete specific functions that cause errors and are not used
    else if(name == "__clang_call_terminate") {
      ii->replaceAllUsesWith(llvm::UndefValue::get(ii->getType()));
      ii->dropAllReferences();
      continue;
    }

    // Add NVVM metadata to specify kernel
    if(name.compare(0, kernelPrefix.length(), kernelPrefix) == 0) {
      std::vector<llvm::Value*> mdVec;
      mdVec.push_back(ii);
      mdVec.push_back(llvm::MDString::get(module->getContext(), "kernel"));
      mdVec.push_back(llvm::ConstantInt::get(module->getContext(), llvm::APInt(32, 1)));

      llvm::MDNode* mdNode =
        llvm::MDNode::get(module->getContext(), llvm::ArrayRef<llvm::Value*>(mdVec));
      nvvmMD->addOperand(mdNode);
    }

    // Remove NVPTX backend calling conventions
    ii->setCallingConv(llvm::CallingConv::C);

    // Iterate over each instruction in the function
    // to remove NVPTX backend calling conventions and
    // to replace Piko atomic instructions with LLVM atomic instructions
    std::vector<llvm::CallInst*> callInsts;
    for(llvm::Function::iterator
        jj = ii->begin(), je = ii->end(); jj != je; ++jj)
    {
      for(llvm::BasicBlock::iterator
          kk = jj->begin(), ke = jj->end(); kk != ke; ++kk)
      {
        if(llvm::CallInst* callInst = llvm::dyn_cast<llvm::CallInst>(&*kk))
        {
          callInst->setCallingConv(llvm::CallingConv::C);

          if(!callInst->getCalledFunction())
            continue;

          // Replace Piko atomic instructions with LLVM atomic instructions
          std::string calledName = callInst->getCalledFunction()->getName();
          if(calledName.find(atomicPrefix) != std::string::npos)
          {
            llvm::AtomicRMWInst::BinOp op;

            if(calledName.compare("__atomic_nvvm_increment__") == 0)
              op = llvm::AtomicRMWInst::Add;
            else if(calledName.compare("__atomic_nvvm_decrement__") == 0)
              op = llvm::AtomicRMWInst::Sub;
            else if(calledName.compare("__atomic_llvm_min__") == 0)
              op = llvm::AtomicRMWInst::Min;
            else if(calledName.compare("_Z24__atomic_llvm_minLocal__PU3AS3ii") == 0)
              op = llvm::AtomicRMWInst::Min;
            else if(calledName.compare("__atomic_llvm_add__") == 0)
              op = llvm::AtomicRMWInst::Add;
            else if(calledName.compare("__atomic_nvvm_addFloat__") == 0)
              continue;
            else {
              llvm::errs() << "Unsupported Atomic Operation: " << calledName << "\n";
              return false;
            }

            llvm::AtomicRMWInst* atomicInst = new llvm::AtomicRMWInst(
              op, callInst->getArgOperand(0), callInst->getArgOperand(1),
              llvm::Monotonic, llvm::CrossThread, callInst);

            callInst->replaceAllUsesWith(atomicInst);
            callInsts.push_back(callInst);
          }
        }
      }
    }

    // Erase calls to Piko atomic instruction placeholders
    for(int j = 0; j < callInsts.size(); ++j) {
      callInsts[j]->eraseFromParent();
    }

    // Remove attributes that are incompatible with NVVM
    llvm::Attributes uwTable =
      llvm::Attributes::get(ii->getContext(), llvm::Attributes::UWTable);
    ii->removeFnAttr(uwTable);
    ii->setAlignment(0);
  }

  // Dump LLVM IR to stderr
  if(pikocOptions.dumpIR)
    module->dump();

  // Backend code generation to PTX
  // TODO allow for backend options
  int nvvmNumOptions = 2;
  const char* nvvmOptions[] = {"-prec-sqrt=0", "-prec-div=0"};
  nvvmProgram program;

  if(nvvmCreateProgram(&program) != NVVM_SUCCESS) {
    llvm::errs() << "Failed to create NVVM compilation unit\n";
    return false;
  }

  // Add libdevice to NVVM compilation unit
  std::string libdeviceFileName = pikocOptions.cudaIncludeDir;
  libdeviceFileName = libdeviceFileName + LIB_DEVICE_PATH;

  std::ifstream libdeviceIn(libdeviceFileName.c_str());
  if(!libdeviceIn.is_open()) {
    llvm::errs() << "Unable to open libdevice file at: " << libdeviceFileName << "\n";
    nvvmDestroyProgram(&program);
    return false;
  }

  std::string libdevice;
  libdeviceIn.seekg(0, std::ios::end);
  libdevice.resize(libdeviceIn.tellg());
  libdeviceIn.seekg(0, std::ios::beg);
  libdeviceIn.read(&libdevice[0], libdevice.size());
  libdeviceIn.close();

  if(nvvmAddModuleToProgram(program, libdevice.c_str(),
      libdevice.size(), "libdevice") != NVVM_SUCCESS)
  {
    llvm::errs() << "Unable to add libdevice module to NVVM compilation unit\n";
    nvvmDestroyProgram(&program);
    return false;
  }

  // Add Piko module to NVVM compilation unit
  std::string pikoModule;
  llvm::raw_string_ostream pikoModuleStream(pikoModule);
  pikoModuleStream << *module;

  if(nvvmAddModuleToProgram(program, pikoModule.c_str(),
      pikoModule.size(), "PikoPipe") != NVVM_SUCCESS)
  {
    llvm::errs() << "Unable to add Piko module to NVVM compilation unit\n";
    nvvmDestroyProgram(&program);
    return false;
  }

  // Verify NVVM compilation unit
  bool exitCompilation = false;
  if(nvvmVerifyProgram(program, nvvmNumOptions, nvvmOptions) != NVVM_SUCCESS) {
    llvm::errs() << "Failed to verify NVVM compilation unit\n";
    exitCompilation = true;
  }

  if(printWarningsAndErrorsNVVM(program) || exitCompilation) {
    nvvmDestroyProgram(&program);
    return false;
  }

  // Compile the NVVM compilation unit
  char* ptx;
  if(nvvmCompileProgram(program, nvvmNumOptions, nvvmOptions) != NVVM_SUCCESS) {
    llvm::errs() << "Failed to generate PTX from NVVM compilation unit\n";
    exitCompilation = true;
  }
  else {
    size_t ptxSize;
    if(nvvmGetCompiledResultSize(program, &ptxSize) != NVVM_SUCCESS) {
      llvm::errs() << "Failed to get the PTX output size\n";
      exitCompilation = true;
    }
    else {
      ptx = (char*) malloc(ptxSize);
      if(ptx == NULL) {
        llvm::errs() << "Failed to allocate memory for PTX output\n";
        exitCompilation = true;
      }
      else if(nvvmGetCompiledResult(program, ptx) != NVVM_SUCCESS) {
        llvm::errs() << "Failed to get the PTX output\n";
        exitCompilation = true;
      }
    }
  }

  if(printWarningsAndErrorsNVVM(program) || exitCompilation) {
    free(ptx);
    nvvmDestroyProgram(&program);
    return false;
  }

  // Print PTX to file
  std::ofstream ptxOut(outFileName.c_str(), std::ios::trunc);
  if(!ptxOut.is_open()) {
    llvm::errs() << "Unable to open PTX output file: " << outFileName << "\n";
    free(ptx);
    nvvmDestroyProgram(&program);
    return false;
  }

  // Print out the .version, .target, and .address_size declarations
  // and everything preceding them
  bool foundVersion = false;
  bool foundTarget = false;
  bool foundAddressSize = false;
  std::stringstream ss(ptx);
  do
  {
    std::string line;
    std::getline(ss, line);

    if(line.substr(0,8) == ".version")
      foundVersion = true;
    else if(line.substr(0,7) == ".target")
      foundTarget = true;
    else if(line.substr(0,13) == ".address_size")
      foundAddressSize = true;

    ptxOut << line << "\n";

    if(foundVersion && foundTarget && foundAddressSize)
      break;

  } while(!ss.eof());

  if(! (foundVersion && foundTarget && foundAddressSize) )
  {
    llvm::errs() << "ERROR: Failed to find .version, .target, and/or .address_size declarations "
                 << "in the PTX output\n";
    ptxOut.close();
    free(ptx);
    nvvmDestroyProgram(&program);
    return false;
  }

  // Print out the vprintf function declaration
  ptxOut << "\n";
  ptxOut << ".extern .func  (.param .b32 func_retval0) vprintf" << "\n";
  ptxOut << "(" << "\n";
  ptxOut << "  .param .b64 vprintf_param_0," << "\n";
  ptxOut << "  .param .b64 vprintf_param_1" << "\n";
  ptxOut << ")" << "\n";
  ptxOut << ";" << "\n";

  // Print out the rest of the PTX code
  ptxOut << ss.rdbuf();
  ptxOut.close();

  free(ptx);
  nvvmDestroyProgram(&program);

	return true;
}

bool PTXBackend::emitAllocateFunc(std::ostream& outfile)
{
  std::string tabs = "";
  std::string pipeName = psum.name;
  bool optimize = pikocOptions.optimize;

  outfile << "#ifdef __PIKOC_HOST__\n";
  outfile << "#ifndef PIKO_" << pipeName << "_ALLOC_AND_RUN_H\n";
  outfile << "#define PIKO_" << pipeName << "_ALLOC_AND_RUN_H\n";
  outfile << "\n";
  outfile << "#include <cuda.h>\n";
  outfile << "#include <builtin_types.h>\n";
  outfile << "#include <GL/glut.h>\n";
  outfile << "\n";

  outfile << "#include \"internal/cudaMacros.h\"\n";
  outfile << "#include <ctime>\n";
  outfile << "\n";  


  outfile << "void " << pipeName << "::allocate(" << psum.constState_type
    << "& h_constState, " << psum.mutableState_type << "& h_mutableState, "
    << psum.input_type << "* inputData, int count)\n";
  outfile << "{\n";
  outfile << "  printf(\"Allocating...\\n\");\n";
  outfile << "  constState_ = &h_constState;\n";
  outfile << "  mutableState_ = &h_mutableState;\n";

  outfile << "  constState_->isConstantState();\n";
  outfile << "  mutableState_->isMutableState();\n";
  outfile << "  inputData[0].isPrim();\n";
  outfile << "  count_ = count;\n";

  outfile << "  // CUDA driver setup\n";
  outfile << "  cuInit(0);\n";
  outfile << "  int deviceCount = 0;\n";
  outfile << "  cuDeviceGetCount(&deviceCount);\n";
  outfile << "  if(deviceCount == 0)\n";
  outfile << "  {\n";
  outfile << "    std::cerr << \"There is no device supporting CUDA.\\n\";\n";
  outfile << "    return;\n";
  outfile << "  }\n";

  outfile << "  cuDeviceGet(&cuDevice, 0); \n";

  outfile << "  cuCtxCreate(&cuContext, 0, cuDevice);\n";

  outfile << "  CUDACHECK(cuModuleLoad(&cuModule, \"__pikoCompiledPipe.ptx\"));\n";

  outfile << "  // Increase the GPU stack size\n";
  outfile << "  CUDACHECK(cuCtxSetLimit(CU_LIMIT_STACK_SIZE, 4096));\n";

  outfile << "  // Create a device pointer for each stage\n";
  outfile << "  // and map the host pointer to the device pointer\n";
  outfile << "  std::map<void*, CUdeviceptr> stgMap;\n";

  for(std::vector<stageSummary>::iterator ii = psum.stages.begin(), ie = psum.stages.end(); ii != ie; ii++)
  {
    std::string stgName = ii->name;
    std::string stgType = ii->fullType;
    outfile << "  CUDACHECK(cuMemAlloc(&d_" << stgName << ", sizeof(" << stgType << ")));\n";
    outfile << "  stgMap[&" << stgName << "] = d_" << stgName << ";\n";
    outfile << "\n";
  }

  outfile << "  // add pikoScreen to the stage map\n";
  outfile << "  pikoScreen.allocate(constState_);\n";

  outfile << "  CUDACHECK(cuMemAlloc(&d_pikoScreen, sizeof(PikoScreen)));\n";
  outfile << "  CUDACHECK(cuMemcpyHtoD(d_pikoScreen, &pikoScreen, sizeof(pikoScreen)));\n";
  outfile << "  stgMap[&pikoScreen] = d_pikoScreen;\n";

  outfile << "  // Make pikoScreen the output of any drain stages\n";
  for(std::vector<stageSummary*>::iterator
      ii = psum.drainStages.begin(), ie = psum.drainStages.end();
      ii != ie; ++ii)
  {
    stageSummary* stg = *ii;
    outfile << "  " << stg->name << ".outPort[0] = &pikoScreen;\n";
  }
  outfile << "\n";


  outfile << "  // Piko initial input data\n";
  outfile << "  h_input.allocate();\n";
  outfile << "  h_input.copyData(inputData, count);\n";
  outfile << "  CUDACHECK(cuMemAlloc(&d_mutableState, sizeof(" << psum.mutableState_type << ")));\n";
  outfile << "  CUDACHECK(cuMemAlloc(&d_input, sizeof(" << psum.input_type << ")));\n";
  outfile << "  CUDACHECK(cuMemcpyHtoD(d_input, &h_input, sizeof(" << psum.input_type << ")));\n";

  outfile << "  // Setup stages\n";
  for(std::vector<stageSummary*>::iterator
      ii = psum.stagesInOrder.begin(), ie = psum.stagesInOrder.end();
      ii != ie; ++ii)
  {
    stageSummary* stg = *ii;
    std::string stgName = stg->name;
    std::string stgType = stg->fullType;

    outfile << "  " << stgName << ".allocate(constState_, d_mutableState, stgMap,";
    if(stg->fusedWithNext)
      outfile << "true";
    else
      outfile << "false";
    outfile << ");\n";
    for(int i=0; i < NUM_PORTS; ++i) {
      outfile << "  " << stgName << ".outPortTypes[" << i << "] = "
        << stg->outPortTypes[i] << ";\n";
    }

    outfile << "  CUDACHECK(cuMemcpyHtoD(d_" << stgName << ", &" << stgName << ",\n"
            << "    sizeof(" << stgType << ")));\n";
  }
  outfile << "\n";



  int curKernel = 0;

  writeKernelFunctionFetch(curKernel, false, outfile);
  curKernel += 1;

  for(std::vector< std::vector<stageSummary*> >::iterator
      ii = kernelList.begin(), ie = kernelList.end();
      ii != ie; ++ii)
  {
    stageSummary* stg = (*ii)[0];

    if(!optimize || !stg->schedules[0].trivial) {
      writeKernelFunctionFetch(curKernel, false, outfile);
      curKernel += 1;
    }

    writeKernelFunctionFetch(curKernel, false, outfile);
    curKernel += 1;
  }
  outfile << "  printf(\"Done...\\n\");\n";
  outfile << "}\n";

  return true;
}

bool PTXBackend::emitPrepareFunc(std::ostream& outfile)
{
  std::string tabs = "  ";
  std::string pipeName = psum.name;
  outfile << "void " << pipeName << "::prepare()\n";
  outfile << "{\n";
  //outfile << "  printf(\"Preparing...\\n\");\n";
  outfile << tabs << "CUdeviceptr d_constState;\n";
  outfile << tabs << "size_t      constStateSize;\n";
  outfile << tabs << "CUDACHECK(cuModuleGetGlobal(&d_constState, &constStateSize, cuModule, \"constState\"));\n";
  outfile << tabs << "CUDACHECK(cuMemcpyHtoD(d_constState, constState_, sizeof("
    << psum.constState_type << ")));\n";
  outfile << tabs << "CUDACHECK(cuMemcpyHtoD(d_mutableState, mutableState_, sizeof("
    << psum.mutableState_type << ")));\n";
  //outfile << "  printf(\"Done...\\n\");\n";
  outfile << "}\n";

  return true;
}

bool PTXBackend::emitRunSingleFunc(std::ostream& outfile)
{
  std::string tabs = "  ";
  std::string pipeName = psum.name;
  outfile << "void " << pipeName << "::run_single()\n";
  outfile << "{\n";
  //outfile << "  printf(\"Run Single...\\n\");\n";
  outfile << "  int numBlocks;\n";
  outfile << "  int numThreads;\n";
  outfile << "  int count = count_;\n";

  writeKernelCalls(tabs, outfile);

  outfile << "  CUDACHECK(cuCtxSynchronize());\n";
  //outfile << "  printf(\"Done...\\n\");\n";
  outfile << "}\n";

  return true;
}

bool PTXBackend::emitDestroyFunc(std::ostream& outfile)
{
  std::string tabs = "  ";
  std::string pipeName = psum.name;
  outfile << "void " << pipeName << "::destroy()\n";
  outfile << "{\n";
  outfile << "  printf(\"Freeing...\\n\");\n";
  outfile << "  // Free stages and input\n";
  outfile << "  h_input.free();\n";
  outfile << "  CUDACHECK(cuMemFree(d_input));\n";

  for(std::vector<stageSummary*>::iterator
      ii = psum.stagesInOrder.begin(), ie = psum.stagesInOrder.end();
      ii != ie; ++ii)
  {
    outfile << "  " << (*ii)->name << ".free();\n";
    outfile << "  CUDACHECK(cuMemFree(d_" << (*ii)->name << "));\n";
  }
  outfile << "\n";

  outfile << "  pikoScreen.free();\n";
  outfile << "  CUDACHECK(cuMemFree(d_pikoScreen));\n";
  outfile << "  CUDACHECK(cuMemFree(d_mutableState));\n";
  outfile << "  CUDACHECK(cuCtxDestroy(cuContext));\n";
  outfile << "  printf(\"Done...\\n\");\n";
  outfile << "}\n";

  outfile << "#endif // PIKO_" << pipeName << "_ALLOC_AND_RUN_H\n";
  outfile << "#endif // __PIKOC_HOST__\n\n";
  return true;
}

void PTXBackend::writeKernelFunctionFetch(int kernelID, bool bAllocate, std::ostream& outfile)
{
  std::ostringstream ss;
  ss << "kernel" << kernelID;
  std::string kernel = ss.str();

  if(bAllocate) outfile << "  CUfunction " << kernel << ";\n";
  outfile << "  CUDACHECK(cuModuleGetFunction(&" << kernel
    << ", cuModule, \"" << kernel << "\"));\n";
  outfile << "\n";
}

void PTXBackend::writeKernelCalls(std::string tabs, std::ostream& outfile)
{
	bool optimize = pikocOptions.optimize;

  int curKernel = 0;
  std::string params;
  std::vector<stageSummary*> loopStgs;

  // Assign Bin
  outfile << tabs << "// AssignBin (first stage only)\n";
  outfile << tabs << "numBlocks = (count / 512) + "
    << "( (count % 512 == 0) ? 0 : 1 );\n";
  outfile << tabs << "numThreads = 512;\n";

  params = "&d_input, &d_" + kernelList[0][0]->name;
  writeKernelRunner(curKernel, params, tabs, outfile);
  outfile << "\n";

  curKernel += 1;
  params = "";

  for(std::vector< std::vector<stageSummary*> >::iterator
      ii = kernelList.begin(), ie = kernelList.end();
      ii != ie; ++ii)
  {
    stageSummary* stg = (*ii)[0];
    std::string stgName = stg->name;
    std::string stgType = stg->fullType;

    outfile << tabs << "int numBins_" << stgName << " = " << stgName << ".getNumBins();\n";
    outfile << "\n";

    if(stg->loopStart) {
      loopStgs.push_back(stg);

      outfile << tabs << "// begin loop\n";
      outfile << tabs << "int hasPrims_" << stgName << ";\n";
      outfile << tabs << "do {\n";

      tabs += "  ";
    }

    // Schedule
    if(!optimize || !stg->schedules[0].trivial) {
      outfile << tabs << "// Schedule\n";
      outfile << tabs << "if(numBins_" << stgName << " < 512) {\n";
      outfile << tabs << "  numBlocks = 1;\n";
      outfile << tabs << "  numThreads = numBins_" << stgName << ";\n";
      outfile << tabs << "}\n";
      outfile << tabs << "else {\n";
      outfile << tabs << "  numBlocks = (numBins_" << stgName << " / 512) + ( (numBins_"
        << stgName << " % 512 == 0) ? 0 : 1 );\n";
      outfile << tabs << "  numThreads = 512;\n";
      outfile << tabs << "}\n";
      outfile << "\n";

      params = "&d_" + stgName;
      writeKernelRunner(curKernel, params, tabs, outfile);
      outfile << "\n";

      curKernel += 1;
    }

    // Process
    outfile << tabs << "// Process\n";
    if(stg->schedules[0].schedPolicy == schedAll) {
      int tileSplitSize = stg->schedules[0].tileSplitSize;
      outfile << tabs << "numBlocks = 1000;\n";
      outfile << tabs << "numThreads = " << stg->threadsPerTile << ";\n";

      outfile << tabs << "int tileSplitSize = " << tileSplitSize << ";\n";
      outfile << tabs << "for(int i = 0; i < numBins_" << stgName << "; ++i) {\n";
      tabs += "  ";
      params = "&d_" + stgName + ", &tileSplitSize, &i";
      writeKernelRunner(curKernel, params, tabs, outfile);
      tabs = tabs.substr(0, tabs.size() - 2);
      outfile << tabs << "}\n";
    }
    else {
      outfile << tabs << "numBlocks = numBins_" << stgName << ";\n";
      outfile << tabs << "numThreads = " << stg->threadsPerTile << ";\n";

      params = "&d_" + stgName;
      writeKernelRunner(curKernel, params, tabs, outfile);
    }
    outfile << "\n";

    if(stg->loopEnd || (optimize && ii->back()->loopEnd) ) {
      stageSummary* loopStg = loopStgs.back();
      std::string loopStgName = loopStg->name;
      std::string loopStgType = loopStg->fullType;
      loopStgs.pop_back();

      outfile << tabs << loopStgType << " *" << "loop_" << loopStgName << " =\n";
      outfile << tabs << "  (" << loopStgType << "*) "
        << " malloc(sizeof(" << loopStg->fullType << "));\n";
      outfile << tabs << "CUDACHECK(cuMemcpyDtoH(loop_" << loopStgName << ", "
        << "d_" << loopStgName << ", sizeof(" << loopStgType << ")));\n";
      outfile << tabs << "hasPrims_" << loopStgName
        << " = loop_" << loopStgName << "->hasPrims;\n";

      tabs = tabs.substr(0, tabs.size() - 2);
      outfile << tabs << "} while(hasPrims_" << loopStgName << ");\n";
      outfile << tabs << "// end loop\n";
      outfile << "\n";
    }

    curKernel += 1;
  }
}

void PTXBackend::writeKernelRunner(int kernelID, std::string params, std::string tabs,
	std::ostream& outfile)
{
  std::ostringstream ss;
  ss << "kernel" << kernelID;
  std::string kernel = ss.str();

  if(pikocOptions.displayGrid)
    outfile << tabs << "printf(\"kernel launch: blocks \%d, thread \%d\\n\",numBlocks, numThreads);\n";
  outfile << tabs << "void* args_" << kernel << "[] = {" << params << "};\n";
  outfile << tabs << "CUDACHECK(cuLaunchKernel(" << kernel << ",";
  outfile << tabs << " numBlocks, 1, 1, numThreads, 1, 1,";
  outfile << tabs << " 0, 0, args_" << kernel << ", 0));\n";
}

bool PTXBackend::printWarningsAndErrorsNVVM(nvvmProgram& program) {
  bool exitCompilation = false;
  size_t nvvmLogSize;
  char* nvvmLog;
  if(nvvmGetProgramLogSize(program, &nvvmLogSize) != NVVM_SUCCESS) {
    llvm::errs() << "Failed to get NVVM compilation log size\n";
    exitCompilation = true;
  }
  else {
    nvvmLog = (char*) malloc(nvvmLogSize);
    if(nvvmLog == NULL) {
      llvm::errs() << "Failed to allocate memory for NVVM compilation log\n";
      exitCompilation = true;
    }
    else if(nvvmGetProgramLog(program, nvvmLog) != NVVM_SUCCESS) {
      llvm::errs() << "Failed to get the NVVM compilation log\n";
      exitCompilation = true;
    }
    else {
      llvm::errs() << nvvmLog << "\n";
    }
    free(nvvmLog);
  }

  return exitCompilation;
}
