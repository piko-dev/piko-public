#include "Backend/CPUBackend.hpp"

#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/TypeBuilder.h"
#include "llvm/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/IPO.h"

bool CPUBackend::emitDefines(std::ostream& outfile) {
  outfile << "#define __PIKOC_CPU__\n";
  outfile << "\n";
  outfile << "#ifndef __PIKOC_DEVICE__\n";
  outfile << "  #define __PIKOC_DEVICE__\n";
  outfile << "#endif // ndef __PIKOC_DEVICE__\n";
  outfile << "\n";

  outfile << "// device-side members\n";
  outfile << "#define __PIKO_DEVICE_MEMBERS__ \\\n";
  outfile << "  StageFloor* d_pikoScreen; \\\n";
  outfile << "  " << psum.mutableState_type << " *d_mutableState; \\\n";
  outfile << "  PikoArray<" << psum.input_type << "> *d_input; \\\n";

  for(std::vector<stageSummary>::iterator
      ii = psum.stages.begin(), ie = psum.stages.end();
      ii != ie; ii++)
  {
    std::string stgName = ii->name;
    std::string stgType = ii->fullType;
    outfile << "  " << stgType << " *d_" << stgName << "; \\\n";
  }
  outfile << "  ;\n\n";

  return true;
}

bool CPUBackend::optimizeLLVMModule(int optLevel)
{
	return true;
}

bool CPUBackend::emitRunFunc(std::ostream& outfile)
{
  std::string tabs = "";
  std::string pipeName = psum.name;
	bool optimize = pikocOptions.optimize;

  outfile << "thread_local int threadIdx_x = 0;\n";
  outfile << "thread_local int blockIdx_x = 0;\n";
  outfile << "int blockDim_x = 0;\n";
  outfile << "\n";
  outfile << "#ifdef __PIKOC_HOST__\n";
  outfile << "#ifndef PIKO_" << pipeName << "_RUNFUNC_H\n";
  outfile << "#define PIKO_" << pipeName << "_RUNFUNC_H\n";
  outfile << "\n";
  outfile << "#include <GL/glut.h>\n";
  outfile << "\n";

  outfile << "#include <cstdio>\n";
  outfile << "#include <cstring>\n";
  outfile << "#include <ctime>\n";
  outfile << "#include <thread>\n";
  outfile << "\n";

  outfile << "unsigned* pixelData;\n";
  outfile << "\n";

  outfile << "void pikoDisplayFunc() {\n";
  outfile << "  //glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);\n";
  outfile << "  glDrawPixels(constState.screenSizeX, constState.screenSizeY,\n";
  outfile << "    GL_RGBA, GL_UNSIGNED_BYTE, pixelData);\n";
  outfile << "  glutSwapBuffers();\n";
  outfile << "}\n";
  outfile << "\n";

  outfile << "void " << pipeName << "::run("
    << psum.constState_type << " &h_constState, " << psum.mutableState_type << "&h_mutableState, "
    << psum.input_type << " *inputData, int count) {\n";

  if(pikocOptions.enableTimers) {
    outfile << "  clock_t totalTime, setupTime, kernelTime;\n";
    outfile << "  totalTime = clock();\n";
    outfile << "  setupTime = clock();\n";
    outfile << "\n";
  }

  outfile << "  h_constState.isConstantState();\n";
  outfile << "  h_mutableState.isMutableState();\n";
  outfile << "  inputData[0].isPrim();\n";
  outfile << "  constState = h_constState;\n";
  outfile << "\n";
  outfile << "// Create a device pointer for each stage\n";
  outfile << "// and map the host pointer to the device pointer\n";
  outfile << "  std::map<void*, StageFloor*> stgMap;\n";

  for(std::vector<stageSummary>::iterator
      ii = psum.stages.begin(), ie = psum.stages.end();
      ii != ie; ii++)
  {
    std::string stgName = ii->name;
    std::string stgType = ii->fullType;
    outfile << "  " << stgType << "* d_" << stgName << ";\n";
    outfile << "  d_" << stgName << " = &" << stgName << ";\n";
    outfile << "  stgMap[&" << stgName << "] = d_" << stgName << ";\n";
    outfile << "\n";
  }

  outfile << "// Create an instance of PikoScreen, call its allocate method,\n";
  outfile << "// and add it to the stage map\n";
  outfile << "  PikoScreen pikoScreen;\n";
  outfile << "  pikoScreen.allocate(&h_constState);\n";
  outfile << "  StageFloor* d_pikoScreen;\n";
  outfile << "  d_pikoScreen = &pikoScreen;\n";
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
  outfile << "  " << psum.mutableState_type << " *d_mutableState;\n";
  outfile << "  PikoArray<" << psum.input_type << "> *d_input;\n";
  outfile << "  PikoArray<" << psum.input_type << "> h_input;\n";
  outfile << "\n";
  outfile << "  h_input.allocate();\n";
  outfile << "  h_input.copyData(inputData, count);\n";
  outfile << "  d_mutableState = (" << psum.mutableState_type << "*) malloc(sizeof("
    << psum.mutableState_type << "));\n";
  outfile << "  d_input = &h_input;\n";
  outfile << "\n";

  outfile << "// Setup stages\n";
  for(std::vector<stageSummary*>::iterator
      ii = psum.stagesInOrder.begin(), ie = psum.stagesInOrder.end();
      ii != ie; ++ii)
  {
    stageSummary* stg = *ii;
    std::string stgName = stg->name;
    std::string stgType = stg->fullType;

    outfile << "  " << stgName << ".allocate(&h_constState, d_mutableState, stgMap, ";
    if(stg->fusedWithNext)
      outfile << "true";
    else
      outfile << "false";
    outfile << ");\n";
    for(int i=0; i < NUM_PORTS; ++i) {
      outfile << "  " << stgName << ".outPortTypes[" << i << "] = "
        << stg->outPortTypes[i] << ";\n";
    }
  }
  outfile << "\n";

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

  outfile << tabs << "memcpy(d_mutableState, &h_mutableState, sizeof(" << psum.mutableState_type << "));\n";
  outfile << tabs << "constState = h_constState;\n";
	outfile << "\n";
  writeKernelCalls(tabs, outfile);

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
  outfile << tabs << "memcpy(d_mutableState, &h_mutableState, sizeof(" << psum.mutableState_type << "));\n";
  outfile << tabs << "constState = h_constState;\n";
	outfile << "\n";
  writeKernelCalls(tabs, outfile);

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
  outfile << "\n";

  for(std::vector<stageSummary*>::iterator
      ii = psum.stagesInOrder.begin(), ie = psum.stagesInOrder.end();
      ii != ie; ++ii)
  {
      outfile << "  " << (*ii)->name << ".free();\n";
  }
  outfile << "\n";

  outfile << "  pikoScreen.free();\n";
  outfile << "  std::free(d_mutableState);\n";
  outfile << "\n";

  if(pikocOptions.enableTimers) {
    outfile << "  totalTime = clock() - totalTime;\n";
    outfile << "  printf(\"\\n\");\n";
    outfile << "  printf(\"Number of timed pipeline runs: " << pikocOptions.numRuns
            << "\\n\");\n";
    outfile << "  printf(\"Setup Time        (sec): %f\\n\", "
            << "((float) setupTime) / CLOCKS_PER_SEC);\n";
		if(pikocOptions.numRuns == 0) {
			outfile << "  printf(\"Avg Kernel Time   (sec): No timed runs\\n\");\n";
		}
		else {
			outfile << "  printf(\"Avg Kernel Time   (sec): %f\\n\", "
				<< "((float) kernelTime) / CLOCKS_PER_SEC / " << pikocOptions.numRuns << ");\n";
		}
    outfile << "  printf(\"Total Time        (sec): %f\\n\", "
            << "((float) totalTime) / CLOCKS_PER_SEC);\n";
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

// We rely on the host compiler to compile the device code for the CPU backend.
// Thus, nothing needs to be done in this function.
bool CPUBackend::emitDeviceCode(std::string filename)
{
	return true;
}

bool CPUBackend::emitAllocateFunc(std::ostream& outfile)
{
  std::string tabs = "";
  std::string pipeName = psum.name;
  bool optimize = pikocOptions.optimize;

  outfile << "#ifdef __PIKOC_HOST__\n";
  outfile << "#ifndef PIKO_" << pipeName << "_ALLOC_AND_RUN_H\n";
  outfile << "#define PIKO_" << pipeName << "_ALLOC_AND_RUN_H\n";
  outfile << "\n";

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

  outfile << "  // Create a device pointer for each stage\n";
  outfile << "  // and map the host pointer to the device pointer\n";
  outfile << "  std::map<void*, StageFloor*> stgMap;\n";

  for(std::vector<stageSummary>::iterator ii = psum.stages.begin(), ie = psum.stages.end(); ii != ie; ii++)
  {
    std::string stgName = ii->name;
    std::string stgType = ii->fullType;
    outfile << "  d_" << stgName << " = &" << stgName << ";\n";
    outfile << "  stgMap[&" << stgName << "] = d_" << stgName << ";\n";
    outfile << "\n";
  }

  outfile << "  // add pikoScreen to the stage map\n";
  outfile << "  pikoScreen.allocate(constState_);\n";

  outfile << "  d_pikoScreen = &pikoScreen;\n";
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
  outfile << "  d_mutableState = (" << psum.mutableState_type << "*) malloc(sizeof("
    << psum.mutableState_type << "));\n";
  outfile << "  d_input = &h_input;\n";
  outfile << "\n";

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
  }
  outfile << "\n";

  outfile << "  printf(\"Done...\\n\");\n";
  outfile << "}\n";

  return true;
}

bool CPUBackend::emitPrepareFunc(std::ostream& outfile)
{
  std::string tabs = "  ";
  std::string pipeName = psum.name;
  outfile << "void " << pipeName << "::prepare()\n";
  outfile << "{\n";
  //outfile << "  printf(\"Preparing...\\n\");\n";
  outfile << tabs << "memcpy(d_mutableState, mutableState_, sizeof(" << psum.mutableState_type << "));\n";
  outfile << tabs << "constState = *constState_;\n";
  //outfile << "  printf(\"Done...\\n\");\n";
  outfile << "}\n";

  return true;
}

bool CPUBackend::emitRunSingleFunc(std::ostream& outfile)
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

  //outfile << "  printf(\"Done...\\n\");\n";
  outfile << "}\n";

  return true;
}

bool CPUBackend::emitDestroyFunc(std::ostream& outfile)
{
  std::string tabs = "  ";
  std::string pipeName = psum.name;
  outfile << "void " << pipeName << "::destroy()\n";
  outfile << "{\n";
  outfile << "  printf(\"Freeing...\\n\");\n";
  outfile << "  // Free stages and input\n";
  outfile << "  h_input.free();\n";

  for(std::vector<stageSummary*>::iterator
      ii = psum.stagesInOrder.begin(), ie = psum.stagesInOrder.end();
      ii != ie; ++ii)
  {
    outfile << "  " << (*ii)->name << ".free();\n";
  }
  outfile << "\n";

  outfile << "  pikoScreen.free();\n";
  outfile << "  std::free(d_mutableState);\n";
  outfile << "  printf(\"Done...\\n\");\n";
  outfile << "}\n";

  outfile << "#endif // PIKO_" << pipeName << "_ALLOC_AND_RUN_H\n";
  outfile << "#endif // __PIKOC_HOST__\n\n";
  return true;
}

void CPUBackend::writeKernelCalls(std::string tabs, std::ostream& outfile)
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

  params = "d_input, d_" + kernelList[0][0]->name;
  writeKernelRunner(curKernel, params, tabs, outfile, false);
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

      params = "d_" + stgName;
      writeKernelRunner(curKernel, params, tabs, outfile, false);
      outfile << "\n";

      curKernel += 1;
    }

    // Process
    outfile << tabs << "// Process\n";
    outfile << tabs << "numBlocks = numBins_" << stgName << ";\n";
    outfile << tabs << "numThreads = 1;\n";
    //outfile << tabs << "numThreads = " << stg->threadsPerTile << ";\n";

    params = "d_" + stgName;
    writeKernelRunner(curKernel, params, tabs, outfile, true);
    outfile << "\n";

    if(stg->loopEnd || (optimize && ii->back()->loopEnd) ) {
      stageSummary* loopStg = loopStgs.back();
      std::string loopStgName = loopStg->name;
      std::string loopStgType = loopStg->fullType;
      loopStgs.pop_back();

      outfile << tabs << loopStgType << " *" << "loop_" << loopStgName << ";\n";
      outfile << tabs << "loop_" << loopStgName << " = d_" << loopStgName << ";\n";
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

void CPUBackend::writeKernelRunner(int kernelID, std::string params, std::string tabs,
  std::ostream& outfile, bool parallel)
{
  std::ostringstream ss;
  ss << "kernel" << kernelID;
  std::string kernel = ss.str();

  if(pikocOptions.displayGrid)
    outfile << tabs << "printf(\"kernel launch: blocks \%d, thread \%d\\n\",numBlocks, numThreads);\n";

  outfile << tabs << "{\n";

  if(parallel)
  {
    outfile << tabs << "  unsigned numCPUThreads = 64;\n";
    outfile << tabs << "  std::vector<std::thread> cpuThreads;\n";
    outfile << "\n";
    outfile << tabs << "  blockDim_x = numThreads;\n";
    outfile << tabs << "  int blocksPerThread = ceil( numBlocks / (float) numCPUThreads);\n";
    outfile << tabs << "  for(int t = 0; t < numCPUThreads; ++t)\n";
    outfile << tabs << "  {\n";
    outfile << tabs << "    cpuThreads.push_back(std::thread([&, this, t]()\n";
    outfile << tabs << "    {\n";
    outfile << tabs << "      int lastBlock = std::min(numBlocks, (t+1) * blocksPerThread);\n";
    outfile << tabs << "      for(int curBlock = t * blocksPerThread; curBlock < lastBlock; ++curBlock)\n";
    outfile << tabs << "      {\n";
    outfile << tabs << "        blockIdx_x = curBlock;\n";
    outfile << tabs << "        for(threadIdx_x = 0; threadIdx_x < numThreads; ++threadIdx_x) {\n";
    outfile << tabs << "          " << kernel << "(" << params << ");\n";
    outfile << tabs << "        }\n";
    outfile << tabs << "      }\n";
    outfile << tabs << "    }\n";
    outfile << tabs << "    ));\n";
    outfile << tabs << "  }\n";

    outfile << tabs << "  for(auto &t : cpuThreads) t.join();\n";
  }

  else
  {
    outfile << tabs << "  blockDim_x = numThreads;\n";
    outfile << tabs << "  for(int curBlock = 0; curBlock < numBlocks; ++curBlock) {\n";
    outfile << tabs << "    blockIdx_x = curBlock;\n";
    outfile << tabs << "    for(threadIdx_x = 0; threadIdx_x < numThreads; ++threadIdx_x) {\n";
    outfile << tabs << "      " << kernel << "(" << params << ");\n";
    outfile << tabs << "    }\n";
    outfile << tabs << "  }\n";
  }
  outfile << tabs << "}\n";
}
