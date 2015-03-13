#ifndef CPU_BACKEND_HPP
#define CPU_BACKEND_HPP

#include "Backend/PikoBackend.hpp"
#include "PikoSummary.hpp"
#include "PikocOptions.hpp"

#include <fstream>
#include <string>
#include <vector>

#include "llvm/Module.h"

class CPUBackend : public PikoBackend {
public:
	explicit CPUBackend(
			const PikocOptions& pikocOptions
		,	PipeSummary& psum
		, std::vector< std::vector<stageSummary*> >& kernelList)
		: PikoBackend(pikocOptions, psum, kernelList)
	{}

	virtual bool createLLVMModule() override { return true; }
	virtual bool optimizeLLVMModule(int optLevel);

	virtual bool emitDefines(std::ostream& outfile);
	virtual bool emitRunFunc(std::ostream& outfile);
	virtual bool emitDeviceCode(std::string filename);

	virtual bool emitAllocateFunc(std::ostream& outfile);
	virtual bool emitPrepareFunc(std::ostream& outfile);
	virtual bool emitRunSingleFunc(std::ostream& outfile);
	virtual bool emitDestroyFunc(std::ostream& outfile);

protected:
	virtual std::string getTargetTriple() { return "x86_64-pc"; }

private:
	void writeKernelCalls(std::string tabs, std::ostream& outfile);
	void writeKernelRunner(int kernelID, std::string params, std::string tabs,
		std::ostream& outfile, bool parallel);
};

#endif // CPU_BACKEND_HPP
