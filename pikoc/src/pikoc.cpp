#include "Backend/CPUBackend.hpp"
#include "Backend/PTXBackend.hpp"
#include "Frontend/PikoAction.hpp"
#include "pikoc.hpp"
#include "PikocOptions.hpp"
#include "PikocParams.hpp"
#include "PikoSummary.hpp"

#include <fstream>
#include <iostream>
#include <limits>
#include <stdio.h>
#include <unistd.h>

#include <nvvm.h>

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

#define GetCurrentDir getcwd

std::string trim(const std::string &str) {
  size_t s = str.find_first_not_of(" \n\r\t");
  size_t e = str.find_last_not_of(" \n\r\t");

  if( (s == std::string::npos) || (e == std::string::npos) )
    return "";
  else
    return str.substr(s, e-s+1);
}

std::vector< std::vector<stageSummary*> > makeKernelList(PipeSummary psum,
																												 bool optimize)
{
	std::vector< std::vector<stageSummary*> > kernelList;

	std::vector<stageSummary*> curKernel;

	for(int i = 0; i < psum.stagesInOrder.size(); ++i) {
		stageSummary* ssum = psum.stagesInOrder[i];

		if(i == 0) {
			curKernel.push_back(ssum);
			continue;
		}

		scheduleSummary& sch = ssum->schedules[0];
		processSummary& pro = ssum->process;

		stageSummary* prevStg = psum.stagesInOrder[i-1];
		processSummary& prevPro = prevStg->process;

		if(!optimize || !sch.trivial || pro.kernelID != prevPro.kernelID) {
			kernelList.push_back(curKernel);
			curKernel.clear();
		}
		else {
			prevStg->fusedWithNext = true;
		}

		curKernel.push_back(ssum);
	}

	kernelList.push_back(curKernel);

	return kernelList;
}

void generateEmitFunc(PipeSummary psum, std::ostream &outfile)
{
	std::string pipeName = psum.name;

	outfile << "#ifdef __PIKOC_DEVICE__\n";
	outfile << "#ifndef PIKO_" << pipeName << "_EMIT_FUNCS_H\n";
	outfile << "#define PIKO_" << pipeName << "_EMIT_FUNCS_H\n\n";

	// Make stageSummary for PikoScreen
	stageSummary pikoScreen;
	pikoScreen.fullType = "PikoScreen";
	pikoScreen.typeNumber = 0;
	pikoScreen.primTypeIn = "Pixel";

	// Add stages to a map by their types
	std::map<std::string, std::vector<stageSummary*> > stgByType;
	for(auto ii = psum.stages.begin(), ie = psum.stages.end(); ii != ie; ++ii)
	{
		stgByType[ii->type].push_back(&(*ii));
	}

	// Generate specialized emit functions for each stage type
	for(auto ii = stgByType.begin(), ie = stgByType.end(); ii != ie; ++ii)
	{
		// For each iteration, use the first stage summary to represent
		// this stage type, since the info used will be the same
		// for all stages of this type after gathering the output stages.
		std::vector<stageSummary*> &ssums = ii->second;
		stageSummary *ssum = ssums[0];

		// For each stage of this type, add the types of it's output stages
		// to a map. Using map for uniqueness, since we only need each type once.
		std::map<int, std::string> outStgs;
		for(auto jj = ssums.begin(), je = ssums.end(); jj != je; ++jj) {
			// If this is a drain stage, add PikoScreen to the map of output stages
			if((*jj)->nextStages.empty()) {
				outStgs[pikoScreen.typeNumber] = pikoScreen.fullType;
				continue;
			}

			for(auto kk = (*jj)->nextStages.begin(), ke = (*jj)->nextStages.end(); kk != ke; ++kk) {
				outStgs[(*kk)->typeNumber] = (*kk)->fullType;
			}
		}

		// AssignBin specialization
		outfile << "void __emitSpecializationAssignBin" << ssum->type << "__(\n";
		outfile << "  " << ssum->primTypeOut << " p, void* nextStg, int outPortType)\n";
		outfile << "{\n";

		if(outStgs.size() == 1) {
			std::string outStgType = outStgs.begin()->second;
			outfile << "    ((" << outStgType << "*) nextStg)->assignBin(p);\n";
		}
		else {
			outfile << "  if(false) {}\n";
			for(auto jj = outStgs.begin(), je = outStgs.end(); jj != je; ++jj) {
				int typeNumber = jj->first;
				std::string outStgType = jj->second;

				outfile << "  else if(outPortType == " << typeNumber << ")\n";
				outfile << "    ((" << outStgType << "*) nextStg)->assignBin(p);\n";
			}
		}
		outfile << "}\n";
		outfile << "\n";

		// Process specialization
		outfile << "void __emitSpecializationProcess" << ssum->type << "__(\n";
		outfile << "  " << ssum->primTypeOut << " p, void* nextStg, int outPortType)\n";
		outfile << "{\n";

		if(outStgs.size() == 1) {
			std::string outStgType = outStgs.begin()->second;
			outfile << "    ((" << outStgType << "*) nextStg)->process(p);\n";
		}
		else {
			outfile << "  if(false) {}\n";
			for(auto jj = outStgs.begin(), je = outStgs.end(); jj != je; ++jj) {
				int typeNumber = jj->first;
				std::string outStgType = jj->second;

				// A stage will never be fused with another stage of the same type.
				// Thus, a stage cannot emit directly to the process function of itself.
				// This is to prevent recursion in the pipeline implementation.
				if(ssum->typeNumber == typeNumber)
					continue;

				outfile << "  else if(outPortType == " << typeNumber << ")\n";
				outfile << "    ((" << outStgType << "*) nextStg)->process(p);\n";
			}
		}
		outfile << "}\n";
		outfile << "\n";

		// Generate definition for the emit functions, which call the specialized emit functions
		outfile << "void " << ssum->type << "::emit(" << ssum->primTypeOut << " p, int outPortNum)\n";
		outfile << "{\n";

		// If there is only one stage of this type, we can optimize the emit function
		// by compile-time checking if it is fused. Also, we can hardcode the outPortNum.
		// Otherwise, this check needs to happen at runtime and outPortNum cannot be hardcoded.
		if(ssums.size() == 1) {
			if(ssum->nextStages.size() > 1) {
				outfile << "    __emitSpecializationAssignBin" << ssum->type << "__(p, d_outPort_[outPortNum], outPortTypes[outPortNum]);\n";
			}
			else {
				if(ssum->fusedWithNext)
					outfile << "    __emitSpecializationProcess" << ssum->type << "__(p, d_outPort_[0], outPortTypes[0]);\n";
				else
					outfile << "    __emitSpecializationAssignBin" << ssum->type << "__(p, d_outPort_[0], outPortTypes[0]);\n";
			}
		}
		else {
			outfile << "  if(fusedWithNext)\n";
			outfile << "    __emitSpecializationProcess" << ssum->type << "__(p, d_outPort_[outPortNum], outPortTypes[outPortNum]);\n";
			outfile << "  else\n";
			outfile << "    __emitSpecializationAssignBin" << ssum->type << "__(p, d_outPort_[outPortNum], outPortTypes[outPortNum]);\n";
		}
		outfile << "}\n";
		outfile << "\n";
	}

	outfile << "#endif // PIKO_" << pipeName << "_EMIT_FUNCS_H\n";
	outfile << "#endif // __PIKOC_DEVICE__\n\n";
}

void writeKernel(int kernelID, std::string params, std::string body,
								 std::ostream& outfile)
{
	outfile << "extern \"C\"\n";
	outfile << "void kernel" << kernelID << "(" << params << ")\n";
	outfile << "{\n";
	outfile << body;
	outfile << "}\n\n";
}

void generateKernels(PipeSummary psum, std::ostream &outfile,
										 std::vector< std::vector<stageSummary*> > kernelList,
										 bool optimize)
{
	std::string pipeName = psum.name;

	outfile << "#ifdef __PIKOC_DEVICE__\n";
	outfile << "#ifndef PIKO_" << pipeName << "_KERNELS_H\n";
	outfile << "#define PIKO_" << pipeName << "_KERNELS_H\n\n";
	outfile << "#include \"internal/datatypes.h\"\n";
	outfile << "#include \"internal/globalVariables.h\"\n";
	outfile << "#include \"piko/stage.h\"\n\n";

	int curKernel = 0;
	std::string params;
	std::string body;

	// AssignBin for first stage
	stageSummary* ssum = psum.stagesInOrder[0];
	std::string stgName = "d_" + ssum->name;
	std::string stgType = ssum->fullType;

	params += "  PikoArray<" + ssum->primTypeIn + ">* input,\n";
	params += "  " + stgType + " *" + stgName + "\n";

	body += "  const int gid = getGID();\n";
	body += "  if(gid >= input->getNumPrims())\n";
	body += "    return;\n";
	body += "\n";
	body += "  " + stgName + "->assignBin((*input)[gid]);\n";

	writeKernel(curKernel, params, body, outfile);
	curKernel += 1;
	params = "";
	body = "";

	for(std::vector< std::vector<stageSummary*> >::iterator
			ii = kernelList.begin(), ie = kernelList.end();
			ii != ie; ++ii)
	{
		ssum = (*ii)[0];
		stgName = "d_" + ssum->name;
		stgType = ssum->fullType;
		scheduleSummary& sch = ssum->schedules[0];
		processSummary& pro = ssum->process;

		params += "  " + stgType + " *" + stgName;

		// Schedule
		if(!optimize || !sch.trivial) {
			body += "  const int gid = getGID();\n";
			body += "  if(gid >= " + stgName + "->getNumBins())\n";
			body += "    return;\n";
			body += "\n";
			body += "  " + stgName + "->schedule(gid);\n";

			writeKernel(curKernel, params, body, outfile);
			curKernel += 1;
			body = "";
		}

		// Process
		if(sch.schedPolicy == schedAll) {
			params += ",\n  const int tileSplitSize,\n  " + std::string("const int binID\n");

			body += "  overrideBinID = binID;\n";
			body += "  Bin<" + ssum->primTypeIn + ">* bin = " + stgName + "->getBin(binID);\n";
			body += "\n";
			body += "  int numPrims = bin->getNumPrims() - (getBlockID() * tileSplitSize);\n";
			body += "  if(numPrims > tileSplitSize)\n";
			body += "    numPrims = tileSplitSize;\n";
			body += "\n";
		}
		else {
			body += "  overrideBinID = -1;\n";
			body += "  const int binID = getBinID();\n";
			body += "  Bin<" + ssum->primTypeIn + ">* bin = " + stgName + "->getBin(binID);\n";
			body += "\n";
			body += "  const int numPrims = bin->getNumPrims();\n";
		}
		body += "  const int tid = getTID();\n";
		body += "  const int numThreads = getNumThreads();\n";
		body += "\n";
		body += "  if(getGID() == 0)\n";
		body += "    " + stgName + "->hasPrims = false;\n";
		body += "\n";
		body += "  for(int i = tid; i < numPrims; i += numThreads) {\n";

		if(psum.hasLoop)
			body += "		 " + ssum->primTypeIn + " prim = bin->fetchPrimAtomic();\n";
		else
			body += "		 " + ssum->primTypeIn + " prim = bin->fetchPrim();\n";

		body += "	   prim.launchIdx = i;\n";
		body += "    " + stgName + "->process(prim);\n";
		//body += "    " + stgName + "->process(bin->fetchPrim(i));\n";
		body += "  }\n";

		if(!psum.hasLoop)
		{
			body += "	 piko::BinSynchronize();\n";
			body += "  if(tid == 0) bin->updatePrimCount(-numPrims);\n";
		}

		writeKernel(curKernel, params, body, outfile);
		curKernel += 1;
		body = "";
		params = "";
	}

	outfile << "#endif // PIKO_" << pipeName << "_KERNELS_H\n";
	outfile << "#endif // __PIKOC_DEVICE__\n\n";
}

void PressEnterToContinue()
{
	std::cout << "Edit __pikoCompiledPipe.h then\n" << std::flush;
	std::cout << "Press ENTER to continue... " << std::flush;
	std::cin.ignore( std::numeric_limits <std::streamsize> ::max(), '\n' );
}

static llvm::cl::extrahelp CommonHelp(clang::tooling::CommonOptionsParser::HelpMessage);
static llvm::cl::extrahelp MoreHelp("\nMore help here\n");

int main(int argc, char *argv[]) {

	PikocOptions pikocOptions = PikocOptions::parseOptions(argc, argv);

	char currentPath[FILENAME_MAX];

	if(!GetCurrentDir(currentPath, sizeof(currentPath))) {
		llvm::errs() << "Unable to get current working directory\n";
		return 3;
	}

	pikocOptions.workingDir = currentPath;

	std::string outFileNameDefines =
		std::string(currentPath) + "/__pikoDefines.h";
	std::string outFileNameH =
		std::string(currentPath) + "/__pikoCompiledPipe.h";
	std::string outFileNameDevice =
		std::string(currentPath) + "/__pikoCompiledPipe";

	std::ofstream outfileDefines(outFileNameDefines.c_str(), std::ios::trunc);
	outfileDefines.flush();
	if(!outfileDefines.good()) {
		llvm::errs() << "Unable to open output file " << outFileNameDefines << "\n";
		return 4;
	}

	std::ofstream outfile(outFileNameH.c_str(), std::ios::trunc);
	outfile.flush();
	if(!outfile.good()) {
		llvm::errs() << "Unable to open output file " << outFileNameH << "\n";
		return 4;
	}

	std::vector<const char*> clangArgs;

	clangArgs.push_back("clang++");
	clangArgs.push_back(pikocOptions.inFileName.c_str());
	clangArgs.push_back("--");
	clangArgs.push_back("-D__PIKOC__");
	clangArgs.push_back("-D__PIKOC_DEVICE__");
	clangArgs.push_back("-D__PIKOC_HOST__");
	clangArgs.push_back("-D__PIKOC_ANALYSIS_PHASE__");
	clangArgs.push_back("-I");
	clangArgs.push_back(pikocOptions.workingDir.c_str());
	clangArgs.push_back("-I");
	clangArgs.push_back(pikocOptions.clangResourceDir.c_str());
	clangArgs.push_back("-I");
	clangArgs.push_back(pikocOptions.pikoIncludeDir.c_str());
	clangArgs.push_back("-I");
	clangArgs.push_back(pikocOptions.cudaIncludeDir.c_str());
	for(int i = 0; i < pikocOptions.includeDirs.size(); ++i) {
		clangArgs.push_back("-I");
		clangArgs.push_back(pikocOptions.includeDirs[i].c_str());
	}

	int clangArgCount = clangArgs.size();
	clang::tooling::CommonOptionsParser optionsParser(clangArgCount, clangArgs.data());
	clang::tooling::ClangTool pikoTool(optionsParser.GetCompilations(),
																		 optionsParser.GetSourcePathList());

	PipeSummary pSum;
	std::map<std::string, stageSummary> stageMap;

	for(int i = 1; i <= NUM_CLANG_PASSES; ++i)
	{
		pikoTool.run(new PikoActionFactory(&pSum, &stageMap, i));
	}

	//pSum.displaySummary();
	pSum.generateKernelPlan(std::cout);
	std::vector< std::vector<stageSummary*> > kernelList =
		makeKernelList(pSum, pikocOptions.optimize);

	if(pSum.stages.size() == 0)
		return 0;

	for(auto ii = pSum.stages.begin(), ie = pSum.stages.end(); ii != ie; ++ii)
	{
		if(ii->loopStart || ii->loopEnd)
		{
			pSum.hasLoop = true;
			break;
		}
	}

	// Emit the pipeline emit functions and the kernels
	outfile << "//////////////////////////// DEVICE CODE ////////////////////////////\n";
	generateEmitFunc(pSum, outfile);
	generateKernels(pSum, outfile, kernelList, pikocOptions.optimize);

	PikoBackend* backend;
	if(pikocOptions.target == pikoc::PTX)
		backend = new PTXBackend(pikocOptions, pSum, kernelList);
	else if(pikocOptions.target == pikoc::CPU)
		backend = new CPUBackend(pikocOptions, pSum, kernelList);

	backend->emitDefines(outfileDefines);
	outfileDefines.flush();
	outfileDefines.close();

	// Emit the pipeline runner function
	outfile << "//////////////////////////// HOST CODE ////////////////////////////\n";
  if(!backend->emitRunFunc(outfile))        { llvm::errs() << "Unable to emit pipe runner function\n";      exit(1); }
  if(!backend->emitAllocateFunc(outfile))   { llvm::errs() << "Unable to emit pipe allocate function\n";    exit(1); }
  if(!backend->emitPrepareFunc(outfile))    { llvm::errs() << "Unable to emit pipe prepare function\n";     exit(1); }
  if(!backend->emitRunSingleFunc(outfile))  { llvm::errs() << "Unable to emit pipe run-single function\n";  exit(1); }
  if(!backend->emitDestroyFunc(outfile))    { llvm::errs() << "Unable to emit pipe destroy function\n";     exit(1); }

	outfile.flush();
	outfile.close();

	// Pause here if the user wants to edit __pikoCompiledPipe.h
	if(pikocOptions.edit)
		PressEnterToContinue();

	// Create LLVM module in backend
	if(!backend->createLLVMModule()) {
		llvm::errs() << "Unable to create LLVM module for backend code generation\n";
		exit(1);
	}

	// Optimize LLVM module in backend
	if(!backend->optimizeLLVMModule(0)) {
		llvm::errs() << "Unable to optimize LLVM module for backend code generation\n";
		exit(1);
	}

	// Emit backend device code
	if(!backend->emitDeviceCode(outFileNameDevice)) {
		llvm::errs() << "Unable to emit device code\n";
		exit(1);
	}

	delete backend;
}
