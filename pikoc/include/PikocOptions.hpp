#ifndef PIKOC_OPTIONS_HPP
#define PIKOC_OPTIONS_HPP

#include "Backend/PikoTargets.hpp"

#include <string>
#include <vector>

class PikocOptions {
public:
	pikoc::Targets target;
	bool optimize;
	bool enableTimers;
	bool dumpIR;
	bool edit;
	bool inlineDevice;
	bool displayGrid;

	std::string osString;

	std::string workingDir;
	std::string clangResourceDir;
	std::string cudaIncludeDir;
	std::string pikoIncludeDir;
	std::string inFileName;

	int numRuns;

	std::vector<std::string> includeDirs;

	PikocOptions() {
		target = pikoc::PTX;
		optimize = false;
		enableTimers = false;
		dumpIR = false;
		edit = false;
		inlineDevice = false;
		displayGrid = false;

		numRuns = 1;
	}

	static void printOptions();
	static PikocOptions parseOptions(int argc, char *argv[]);
};

#endif // PIKOC_OPTIONS_HPP
