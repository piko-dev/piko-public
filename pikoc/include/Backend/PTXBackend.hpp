#ifndef PTX_BACKEND_HPP
#define PTX_BACKEND_HPP

#include "Backend/PikoBackend.hpp"
#include "PikoSummary.hpp"
#include "PikocOptions.hpp"

#include <fstream>
#include <string>
#include <vector>

#include <nvvm.h>

#include "llvm/Module.h"

class PTXBackend : public PikoBackend {
public:
	explicit PTXBackend(
			const PikocOptions& pikocOptions
		,	PipeSummary& psum
		, std::vector< std::vector<stageSummary*> >& kernelList)
		: PikoBackend(pikocOptions, psum, kernelList)
	{}

	virtual bool optimizeLLVMModule(int optLevel);

	virtual bool emitDefines(std::ostream& outfile);
	virtual bool emitRunFunc(std::ostream& outfile);
	virtual bool emitDeviceCode(std::string filename);

	virtual bool emitAllocateFunc(std::ostream& outfile);
	virtual bool emitPrepareFunc(std::ostream& outfile);
	virtual bool emitRunSingleFunc(std::ostream& outfile);
	virtual bool emitDestroyFunc(std::ostream& outfile);

protected:
	virtual std::string getTargetTriple() { return "nvptx64-nvidia"; }

private:
	void writeKernelFunctionFetch(int kernelID, bool bAllocate, std::ostream& outfile);
	void writeKernelCalls(std::string tabs, std::ostream& outfile);
	void writeKernelRunner(int kernelID, std::string params, std::string tabs,
		std::ostream& outfile);
	bool printWarningsAndErrorsNVVM(nvvmProgram& program);
};

#endif // PTX_BACKEND_HPP
