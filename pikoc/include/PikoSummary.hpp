#ifndef PIKO_SUMMARY_HPP
#define PIKO_SUMMARY_HPP

#include "common_inline.h"

#include <climits>
#include <string>
#include <utility>

// --------------------
//  enums
// --------------------

enum eAssignPolicy{
  assignCustom = 0,
  assignInBin,
	assignBoundingBox,
	assignPosition,
  assignEmpty,
  assignCount,
};

enum eSchedPolicy{
  schedCustom = 0,
  schedLoadBalance,
  schedDirectMap,
  schedSerialize,
  schedAll,
  schedCount,
};

enum eWaitPolicy{
  waitNone = 0,
  waitCustom,
  waitBatch,
  waitEndStage,
  waitEndBin,
  waitCount,
};

enum eArchs{
  archGPU = 0,
  archCPU,
  archIVB,
  archCount,
};

enum eProcPolicy{
  procEmpty = 0,
  procCustom,
  procCount,
};

// --------------------
// convertors
// --------------------

// from string

inline eAssignPolicy  toAssignPolicy(const std::string& s){
  if     (s=="Custom")    	return assignCustom;
  else if(s=="InBin")     	return assignInBin;
	else if(s=="BoundingBox") return assignBoundingBox;
	else if(s=="Position")		return assignPosition;
  else if(s=="Empty")				return assignEmpty;
  else assert(0);

  return assignCount;
}
inline eSchedPolicy   toSchedPolicy (const std::string& s){
  if     (s=="Custom")      return schedCustom;
  else if(s=="LoadBalance") return schedLoadBalance;
  else if(s=="DirectMap")   return schedDirectMap;
  else if(s=="Serialize")   return schedSerialize;
  else if(s=="All")         return schedAll;
  else assert(0);

  return schedCount;
}
inline eWaitPolicy    toWaitPolicy  (const std::string& s){
  if     (s=="None")      return waitNone;
  else if(s=="Custom")    return waitCustom;
  else if(s=="Batch")     return waitBatch;
  else if(s=="EndStage")  return waitEndStage;
  else if(s=="EndBin")    return waitEndBin;
  else assert(0);

  return waitCount;
}
inline eArchs         toArch       (const std::string& s){
  if     (s=="GPU")       return archGPU;
  else if(s=="CPU")       return archCPU;
  else if(s=="IVB")       return archIVB;
  else assert(0);

  return archCount;
}

inline eProcPolicy    toProcPolicy(const std::string& s){
  if     (s=="Empty")     return procEmpty;
  else if(s=="Custom")    return procCustom;
  else assert(0);

  return procCount;
}


// to string

inline std::string toString(const eAssignPolicy& e){

  switch(e){
    case assignCustom:    	return "Custom";  break;
    case assignInBin:     	return "InBin";   break;
    case assignEmpty:     	return "Empty";   break;
		case assignBoundingBox:	return "BoundingBox"; break;
		case assignPosition:		return "Position"; break;
    default:              	return "error";   break;
  };
}
inline std::string toString(const eSchedPolicy& e){

  switch(e){
    case schedCustom:       return "Custom";    break;
    case schedLoadBalance:  return "LoadBalance";break;
    case schedDirectMap:    return "DirectMap"; break;
    case schedSerialize:    return "Serialize"; break;
    case schedAll:          return "All"; break;
    default:                return "error";     break;
  };

}
inline std::string toString(const eWaitPolicy& e){

  switch(e){
    case waitNone:          return "None";      break;
    case waitCustom:        return "Custom";    break;
    case waitBatch:         return "Batch";     break;
    case waitEndStage:      return "EndStage";  break;
    case waitEndBin:        return "EndBin";    break;
    default:                return "error";     break;
  };
}
inline std::string toString(const eArchs& e){
  switch(e){
    case archCPU:           return "CPU";   break;
    case archGPU:           return "GPU";   break;
    case archIVB:           return "IVB";   break;
    default:                return "error"; break;
  };
}

inline std::string toString(const eProcPolicy p){
  switch(p){
    case procEmpty:       return "Empty";  break;
    case procCustom:      return "Custom"; break;
    default:              return "error";  break;
  };
}

// --------------------
//  classes
// --------------------

class stageSummary;

class assignBinSummary{
public:
  std::string  	           codeFile;
  std::string  	           sourceCode;
  eAssignPolicy        policy;
  int                  kernelID;
  int                  bucketLoopLevel;
	int									 bucketLoopID;
	bool								 trivial;

  assignBinSummary(){
    codeFile          = "noAssignFile";
		sourceCode				= "";
    policy            = assignCustom;
    kernelID  = -1;
    bucketLoopLevel   = 0;
		bucketLoopID			= 0;
		trivial						= false;
  }

};

class scheduleSummary{
public:
  std::string                  codeFile;
  std::string									sourceCode;
  eArchs                  arch;
  eSchedPolicy            schedPolicy;
	int											tileSplitSize;
  eWaitPolicy             waitPolicy;
  int                     waitBatchSize;
  std::string                  endStageName;
  stageSummary*           endStagePtr;
  int                     kernelID;
  int                     bucketLoopLevel;
	int											bucketLoopID;
  bool                    isPreScheduleCandidate;
	bool										trivial;

  scheduleSummary(){
    codeFile                = "noSchedFile";
		sourceCode							= "";
    arch                    = archGPU;
    schedPolicy             = schedCustom;
    tileSplitSize           = 0;
    waitPolicy              = waitNone;
    waitBatchSize           = 0;
    kernelID                = -1;
    bucketLoopLevel         = 0;
		bucketLoopID						= 0;
    isPreScheduleCandidate  = false;
    endStageName            = "";
    endStagePtr             = NULL;
		trivial										= false;
  }
};

class processSummary{
public:
  std::string                  codeFile;
  std::string									sourceCode;
  int                     maxOutPrims;
  int                     kernelID;
  int                     bucketLoopLevel;
	int											bucketLoopID;
  eProcPolicy             policy;
	bool										trivial;

  processSummary(){
    codeFile    = "noProcessFile";
		sourceCode	= "";
    maxOutPrims = 1;
    kernelID  = -1;
    bucketLoopLevel = 0;
		bucketLoopID		= 0;
    policy      = procCustom;
		trivial				= false;
  }
};

class stageSummary{
public:
  std::string                  name;
  std::string                  type;
  std::string                  fullType;
  int                     typeNumber;
  std::string                  codeFile;
  vec2i                   binsize;
	int											threadsPerTile;
	std::string									primTypeIn;
	std::string									primTypeOut;
  std::vector<stageSummary*>   nextStages;
  std::vector<stageSummary*>   prevStages;
  std::vector<std::string>          nextStageNames;
	std::vector< std::vector<stageSummary*> > nextStagesByPort;
  assignBinSummary        assignBin;
  std::vector<scheduleSummary> schedules;
  processSummary          process;
  bool                    fusedWithNext;
	bool                    loopStart;
	bool                    loopEnd;
  int                     outPortTypes[5];

  int                     distFromDrain;

  static bool higherDrainDist (const stageSummary& i, const stageSummary& j) { 
    return (i.distFromDrain > j.distFromDrain); 
  }

  stageSummary(){
    name						= "nostage";
    codeFile				= "noStageCodeFile";
    binsize					= vec2i(0,0); // 0,0 indicates fullscreen bin
		threadsPerTile	= 1;
    distFromDrain		= INT_MAX;
    schedules.resize(archCount);
		for(int i=0; i<5; ++i) {
			std::vector<stageSummary*> tmp;
			nextStagesByPort.push_back(tmp);
		}
		fusedWithNext = false;
		loopStart = false;
		loopEnd = false;

		outPortTypes[0] = 0;
		for(int i=1; i<5; ++i) {
			outPortTypes[i] = -1;
		}
  }

	std::string toString() {
		std::stringstream ss;

		ss <<  "name: " << name << "\n";
		ss << "  type: " << type << "\n";
		ss << "  fullType: " << fullType << "\n";
		ss << "  bin size: " << binsize.x() << ", " << binsize.y() << "\n";
		ss << "  threads per tile: " << threadsPerTile << "\n";
		ss << "  prim type - in: " << primTypeIn << "\n";
		ss << "  prim type - out: " << primTypeOut << "\n";
		ss << "  outPortTypes[0]: " << outPortTypes[0] << "\n";
		ss << "  outPortTypes[1]: " << outPortTypes[1] << "\n";
		ss << "  outPortTypes[2]: " << outPortTypes[2] << "\n";
		ss << "  outPortTypes[3]: " << outPortTypes[3] << "\n";
		ss << "  outPortTypes[4]: " << outPortTypes[4] << "\n";

		return ss.str();
	}

	void findKernelOrder(int kernelID, int batch, std::vector< std::pair<int,std::string> > *order);
	bool isCurOrPrevStage(stageSummary *stg);
};


class PipeSummary{


  void          updateDrainDistance(stageSummary* stage);
  bool          canFuse(stageSummary& s1, stageSummary& s2, int whichSchedule, std::vector<stageSummary*>& doneStages);

public:

  std::string               name;
  std::string               filename;
  std::vector<stageSummary> stages;
  std::vector<stageSummary*> stagesInOrder;
	std::vector<stageSummary*> drainStages;

  std::string         constState_type; // pipeline state type
  std::string         mutableState_type; // pipeline state type
  std::string         input_type; // pipeline input type

	bool								 preferDepthFirst;
	bool								hasLoop;

  PipeSummary(){
    name              = "nopipe";
    filename          = "unknown.piko";
		preferDepthFirst  = true;
		hasLoop						= false;
  }

  stageSummary* findStageByName(const std::string& stageName);
	std::vector<stageSummary*> findStageByType(const std::string& typeName);

  void displaySummary();

  void processLinks();

  void generateKernelPlan(std::ostream& outfile);
};


#endif //PIKO_SUMMARY_HPP
