#include "PikocOptions.in"

#include "PikocOptions.hpp"

#include <sstream>
#include <sys/stat.h>

#include "llvm/Support/raw_ostream.h"

void PikocOptions::printOptions() {
	llvm::errs() << "\n";

	llvm::errs() << "Usage: pikoc [options] <main file>\n\n";

	llvm::errs() << "Options:\n";
	llvm::errs() << "  -h, --help            Prints this help message\n";
	llvm::errs() << "  -Idir                 Adds directory 'dir' to include search path\n";
	llvm::errs() << "  --opt                 Enable Piko optimizations\n";
	llvm::errs() << "  --timer               Time the pipeline execution\n";
	llvm::errs() << "  --dumpIR              Print LLVM IR to stderr\n";
	llvm::errs() << "  --numRuns=<x>         Runs the pipeline x times for average timing (default is 1)\n";
	llvm::errs() << "  --target=<target>     Specifies the backend target for device code. Options are:\n";
	llvm::errs() << "                          PTX (default)\n";
	llvm::errs() << "                          CPU\n";
	llvm::errs() << "  --edit                Pauses before PTX generation to allow editing of __pikoCompiledPipe.h\n";
	llvm::errs() << "  --inline-device       Inline all device functions (if possible)\n";

	llvm::errs() << "\n";
}

PikocOptions PikocOptions::parseOptions(int argc, char *argv[]) {

	if(argc < 2) {
		printOptions();
		exit(10);
	}

	PikocOptions options;

	int i;
	for(i = 1; i < argc; ++i) {
		std::string arg = argv[i];

		if(arg.substr(0,1) != "-")
			break;

		if(arg == "-h" || arg == "--help") {
			printOptions();
			exit(0);
		}
		else if(arg == "--opt") {
			options.optimize = true;
		}
		else if(arg == "--timer") {
			options.enableTimers = true;
		}
		else if(arg == "--dumpIR") {
			options.dumpIR = true;
		}
		else if(arg == "--edit") {
			options.edit = true;
		}
		else if(arg.substr(0, 10) == "--numRuns=") {
			std::string num = arg.substr(10);
			std::stringstream ss(num);
			if(!(ss >> options.numRuns)) {
				llvm::errs() << "number of runs must be an integer\n";
				exit(10);
			}
		}
		else if(arg.substr(0,9) == "--target=") {
			std::string t = arg.substr(9);
			if(t == "PTX")
				options.target = pikoc::PTX;
			else if(t == "CPU")
				options.target = pikoc::CPU;
			else {
				llvm::errs() << "Unrecognized target.\n\n";
				printOptions();
			}
		}
		else if(arg == "--inline-device") {
			options.inlineDevice = true;
		}
		else if(arg == "--displaygrid") {
		  options.displayGrid = true;
		}
		else if(arg.substr(0, 2) == "-I") {
			std::string dir = arg.substr(2);

			struct stat status;

			if( (stat(dir.c_str(), &status) == 0) && (status.st_mode & S_IFDIR) ) {
				options.includeDirs.push_back(dir);
			}
			else {
				llvm::errs() << "Directory to include does not exist: " << dir << "\n";
				exit(10);
			}
		}
		else {
			llvm::errs() << "\nInvalid commandline options: " << arg << "\n";
			printOptions();
			exit(10);
		}
	}

	options.osString = OS_STRING;

	options.clangResourceDir = CLANG_RESOURCE_PATH;
	options.cudaIncludeDir = CUDA_INCLUDE_PATH;
	options.pikoIncludeDir = PIKOC_API_PATH;
	options.inFileName = argv[i];

	return options;
}
