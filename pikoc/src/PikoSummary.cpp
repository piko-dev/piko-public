#include "PikoSummary.hpp"

#include <algorithm>
#include <sstream>

using namespace std;

bool compBranchesFurthest(vector<stageSummary*> a, vector<stageSummary*> b) {
	return (a[0]->distFromDrain > b[0]->distFromDrain);
}

bool compBranchesClosest(vector<stageSummary*> a, vector<stageSummary*> b) {
	return (a[0]->distFromDrain < b[0]->distFromDrain);
}

bool isInCycleRecur(stageSummary *target, stageSummary *path, vector<stageSummary*> visited) {
	// if a stage looped around to itself
	if(target == path)
		return true;
	else {
		// if we haven't been to this stage already
		if(std::find(visited.begin(), visited.end(), path) == visited.end()) {
			visited.push_back(path);
			bool ret = false;
			for(unsigned i=0; i<path->prevStages.size(); i++) {
				ret |= isInCycleRecur(target, path->prevStages[i], visited);
			}
			return ret;
		}
		else
			return false;
	}
}

bool isInCycle(stageSummary *target, stageSummary *path) {
	vector<stageSummary*> visited;
	return isInCycleRecur(target, path, visited);
}

bool branchReady(vector<stageSummary*> branch, vector<stageSummary*> doneStages, int whichSchedule) {
	vector<stageSummary*> ds = doneStages;
	for(unsigned i=0; i<branch.size(); i++) {
		if(branch[i]->prevStages.size() == 0) {
				ds.push_back(branch[i]);
		}
		else {
			// check endStage dependencies
			if(branch[i]->schedules[whichSchedule].endStagePtr!=NULL &&
					std::find(ds.begin(), ds.end(), branch[i]->schedules[whichSchedule].endStagePtr) ==
					ds.end()) return false;
			// check previous stage dependencies
			for(unsigned j=0; j<branch[i]->prevStages.size(); j++) {
				stageSummary *curPrevStage = branch[i]->prevStages[j];
				// if a previous stage is not the current stage (for self-cyclic stages) and 
				// the previous stage is not done yet
				if(/*curPrevStage != branch[i] && */std::find(ds.begin(), ds.end(), curPrevStage) == ds.end() &&
						!isInCycle(branch[i], curPrevStage))
					return false;
				else
					ds.push_back(branch[i]);
			}
		}
	}
	return true;
}

// generate kernel plan
void PipeSummary::generateKernelPlan(ostream& outfile){

  // step 1: discover drain nodes
  //printf("-----------\n");
  printf("%s (%s)\n",this->name.c_str(),this->filename.c_str());
  for(unsigned printi=0; printi<(this->name.length() + this->filename.length() + 3); printi++)
    printf("="); printf("\n");
  printf("* Drain stages: ");
  for(unsigned i=0; i<stages.size(); i++){
    if(stages[i].nextStages.size() == 0){
      printf("[%s] ",stages[i].name.c_str());
      drainStages.push_back(&stages[i]);

      stages[i].distFromDrain=0;
      updateDrainDistance(&stages[i]);
    }
  }
  printf("\n");

  //printf("Drain distances:\n");
  //for(unsigned i=0; i<stages.size(); i++){
  //  printf("\t[%d] %s\n",stages[i].distFromDrain, stages[i].name.c_str());
  //}

  // step 2. sort by drain distances
  //sort(stages.begin(), stages.end(), stageSummary::higherDrainDist);

  // step 3. discover preschedule candidates
  for(unsigned i=0; i<stages.size(); i++){
    if(stages[i].schedules[0].schedPolicy == schedDirectMap ||
       stages[i].schedules[0].schedPolicy == schedSerialize){
        stages[i].schedules[0].isPreScheduleCandidate = true;
    }
  }

	// divide pipeline into branches
  vector< vector<stageSummary*> > branches;
	{
		vector<stageSummary*> inBranch;

		// create branches and add first stage to each
		// new branch created if the stage:
		// 1) my only parent is myself
		// 2) has no previous stage OR 
		// 3) has multiple previous stages OR
		// 4) is the child of a stage with mulitple next stages
		for(unsigned i=0; i<stages.size(); i++) {
			bool isInBranch = std::find(inBranch.begin(), inBranch.end(), &stages[i]) != inBranch.end();

			// If my only parent is myself, make new branch
			if(!isInBranch && stages[i].prevStages.size() == 1 && stages[i].prevStages[0] == &stages[i]) {
				vector<stageSummary*> tmp;
				tmp.push_back(&stages[i]);
				inBranch.push_back(&stages[i]);
				branches.push_back(tmp);
			}

			// Does 2 and 3
			else if(!isInBranch && (stages[i].prevStages.size() == 0 || stages[i].prevStages.size() > 1) ) {
				vector<stageSummary*> tmp;
				tmp.push_back(&stages[i]);
				inBranch.push_back(&stages[i]);
				branches.push_back(tmp);
			}
			// Does 4
			if(stages[i].nextStages.size() > 1) {
				if(stages[i].nextStages.size() == 2 &&
						(stages[i].nextStages[0] == &stages[i] || stages[i].nextStages[1] == &stages[i]) ) {
				}

				else {
					for(unsigned j=0; j<stages[i].nextStages.size(); j++) {
						stageSummary *candidate = stages[i].nextStages[j];
						if(std::find(inBranch.begin(), inBranch.end(), candidate) == inBranch.end()) {
							vector<stageSummary*> tmp;
							tmp.push_back(candidate);
							inBranch.push_back(candidate);
							branches.push_back(tmp);
						}
					}
				}
			}
		}

		// find other stages in each branch
		for(unsigned i=0; i<branches.size(); i++) {
			int stgNum = 0;
			bool done = false;
			while(!done) {
				if(branches[i][stgNum]->nextStages.size() == 0)
					done = true;
				else {
					stageSummary *candidate = branches[i][stgNum]->nextStages[0];
					if(candidate == branches[i][stgNum] && branches[i][stgNum]->nextStages.size() > 1)
						candidate = branches[i][stgNum]->nextStages[1];
					// if stage not already in a branch
					if(std::find(inBranch.begin(), inBranch.end(), candidate) == inBranch.end()) {
						// if stage has a Custom dependency, push to new branch
						if(candidate->schedules[0].waitPolicy == waitCustom) {
								vector<stageSummary*> tmp;
								tmp.push_back(candidate);
								inBranch.push_back(candidate);
								branches.push_back(tmp);
								done = true;
						}
						// else if stage has EndStage dependency
						else if(candidate->schedules[0].waitPolicy == waitEndStage) {
							// if the EndStagePtr is not in the current branch
							if(std::find(branches[i].begin(), branches[i].end(), candidate->schedules[0].endStagePtr) == branches[i].end()) {
								// push to new branch
								vector<stageSummary*> tmp;
								tmp.push_back(candidate);
								inBranch.push_back(candidate);
								branches.push_back(tmp);
								done = true;
							}
							else {
								// add to current branch
								branches[i].push_back(candidate);
								inBranch.push_back(candidate);
								stgNum += 1;
							}
						}
						else {
							branches[i].push_back(candidate);
							inBranch.push_back(candidate);
							stgNum += 1;
						}
					}
					else
						done = true;
				}
			}
		}

/*
for(unsigned i=0; i<branches.size(); i++) {
	for(unsigned j=0; j<branches[i].size(); j++) {
		printf("Branch %d Stage %d - %s\n",i,j,branches[i][j]->name.c_str());
	}
}
*/

		// sort branches by distFromDrain - furthest comes first or closest comes first
		sort(branches.begin(), branches.end(), compBranchesFurthest);			// furthest from drain are scheduled first
		//sort(branches.begin(), branches.end(), compBranchesClosest);		// closest drain are scheduled first
	}

	// schedule branches in this order:
	// furthest from drain stages come first
	// if dependent on another branch, skip to next branch and come back afterwards

  int curKernelID         = 0;
	int curBranchNum        = 0;
	int curBucketLoopLevel  = 0;
	int lastBucketLoopLevel = 0;
	int curBucketLoopID			= -1;

  vector<stageSummary*> doneStages;
	while(branches.size() > 0) {
		if(curBranchNum == branches.size())
			break;

		vector<stageSummary*> curBranch = branches[curBranchNum];
//printf("attempting %d\n", curBranchNum);
		if( branchReady(curBranch, doneStages, 0) ) {
//printf("scheduling %d\n", curBranchNum);
			// schedule branch
			vector<stageSummary*> almostDoneStages;
			vec2i lastBinsize = curBranch[0]->binsize;
			for(unsigned i=0; i<curBranch.size(); i++) {
				stagesInOrder.push_back(curBranch[i]);
				assignBinSummary& ass = curBranch[i]->assignBin;
				scheduleSummary&  sch = curBranch[i]->schedules[0];
				processSummary&   pro = curBranch[i]->process;

				int nextKernelID = curKernelID+1;
				int lastBucketLoopLevel = curBucketLoopLevel;
				int lastBucketLoopID = curBucketLoopID;

				if(preferDepthFirst && sch.schedPolicy != schedAll) {
					if(i>0) {
						if(curBranch[i-1]->process.bucketLoopLevel == 0 || sch.waitPolicy == waitEndStage)
							curBucketLoopLevel = 0;
						else {
							//bool sameBinSize = (curBranch[i]->binsize == (curBranch[i-1]->binsize));
							//bool secondLarger = (curBranch[i]->binsize[0] > (curBranch[i-1]->binsize[0])
							//										|| curBranch[i]->binsize[1] > (curBranch[i-1]->binsize[1]));
							//bool secondLarger = (curBranch[i]->binsize[0] > lastBinsize[0]
							//										|| curBranch[i]->binsize[1] > lastBinsize[1]);
							int lastBinX = (lastBinsize[0] == 0) ? INT_MAX : lastBinsize[0];
							int lastBinY = (lastBinsize[1] == 0) ? INT_MAX : lastBinsize[1];
							int curBinX =  (curBranch[i]->binsize[0] == 0) ? INT_MAX : curBranch[i]->binsize[0];
							int curBinY =  (curBranch[i]->binsize[1] == 0) ? INT_MAX : curBranch[i]->binsize[1];
							bool secondLarger = (curBinX > lastBinX || curBinY > lastBinY);
							if(secondLarger)	curBucketLoopLevel = 0;
							else {
								curBucketLoopLevel = lastBucketLoopLevel;
								curBucketLoopID = lastBucketLoopID;
							}
						}
					}
					else {
						curBucketLoopLevel = 0;
					}
				}
				else {
					if(sch.schedPolicy != schedAll) curBucketLoopLevel = 0;
					else if(sch.waitPolicy  == waitEndStage) {
						curBucketLoopLevel = 1;
						curBucketLoopID = lastBucketLoopID+1;
						lastBinsize = curBranch[i]->binsize;
					}
					else {
						if(i>0){
								//&& !(sch.binsize==(curBranch[i-1]->schedules[0].binsize)))
							if(curBranch[i-1]->process.bucketLoopLevel == 0) {
								curBucketLoopLevel = 1;
								curBucketLoopID = lastBucketLoopID+1;
							}
							else {
								//bool secondLarger = (curBranch[i]->binsize[0] > lastBinsize[0]
								//											|| curBranch[i]->binsize[1] > lastBinsize[1]);
								bool sameBinSize = (curBranch[i]->binsize == lastBinsize);
								int lastBinX = (lastBinsize[0] == 0) ? INT_MAX : lastBinsize[0];
								int lastBinY = (lastBinsize[1] == 0) ? INT_MAX : lastBinsize[1];
								int curBinX =  (curBranch[i]->binsize[0] == 0) ? INT_MAX : curBranch[i]->binsize[0];
								int curBinY =  (curBranch[i]->binsize[1] == 0) ? INT_MAX : curBranch[i]->binsize[1];
								bool secondLarger = (curBinX > lastBinX || curBinY > lastBinY);
								if(sameBinSize) {
									curBucketLoopLevel = lastBucketLoopLevel;
									curBucketLoopID = lastBucketLoopID;
								}
								else if(secondLarger) {
									curBucketLoopLevel = 1;
									curBucketLoopID = lastBucketLoopID+1;
								}
								else {
									curBucketLoopLevel = lastBucketLoopLevel+1;
									curBucketLoopID = lastBucketLoopID+1;
								}
							}
						}
						else{
							curBucketLoopLevel = 1;
							curBucketLoopID = lastBucketLoopID+1;
						}
						lastBinsize = curBranch[i]->binsize;
					}
				}

				ass.kernelID = curKernelID;
				ass.bucketLoopLevel = curBucketLoopLevel;
				ass.bucketLoopID = (curBucketLoopLevel == 0) ? -1 : curBucketLoopID;

				if(i==0 && ass.policy != assignEmpty)
					curKernelID = nextKernelID;

				if(i>0 && !canFuse(*curBranch[i-1],*curBranch[i],0,doneStages)) {
					curKernelID = nextKernelID;
					for(unsigned j=0; j<almostDoneStages.size(); j++)
						doneStages.push_back(almostDoneStages[j]);
					almostDoneStages.clear();
					//doneStages.push_back(curBranch[i-1]);
				}
				almostDoneStages.push_back(curBranch[i]);

				// if last iteration, add all almostDoneStages to doneStages
				if(i == curBranch.size()-1) {
					for(unsigned j=0; j<almostDoneStages.size(); j++)
						doneStages.push_back(almostDoneStages[j]);
					almostDoneStages.clear();
				}

				sch.kernelID = curKernelID;
				sch.bucketLoopLevel = curBucketLoopLevel;
				sch.bucketLoopID = (curBucketLoopLevel == 0) ? -1 : curBucketLoopID;
				
				// TODO: Think about what PreScheduleCandiate means, and whether schedAll should be a preschedule candidate
				if(sch.isPreScheduleCandidate) sch.kernelID = ass.kernelID;
				else if((sch.schedPolicy != schedLoadBalance
					&& sch.schedPolicy != schedAll) || !sch.trivial) curKernelID += 1;

				pro.kernelID = curKernelID;
				pro.bucketLoopLevel = curBucketLoopLevel;
				pro.bucketLoopID = (curBucketLoopLevel == 0) ? -1 : curBucketLoopID;

				for(unsigned k=0; k<curBranch[i]->nextStages.size(); k++) {
					stageSummary *tmp = curBranch[i]->nextStages[k];
					if(std::find(doneStages.begin(), doneStages.end(), tmp) != doneStages.end() ||
							std::find(almostDoneStages.begin(), almostDoneStages.end(), tmp)
							!= almostDoneStages.end())
					{
						tmp->loopStart = true;
						curBranch[i]->loopEnd = true;
						printf("\nRepeat kernels %d through %d as necessary\n\n",tmp->assignBin.kernelID,curKernelID);
					}
				}

				// special case for self-cyclic cycles
				if(curBranch[i]->nextStages.size() == 2 &&
						(curBranch[i]->nextStages[0] == curBranch[i]
							|| curBranch[i]->nextStages[1] == curBranch[i]) ) {
						curKernelID += 1;
				}
			}

			// remove branch from list
			branches.erase(branches.begin() + curBranchNum);
			curBranchNum = 0;
			curKernelID += 1;
		}
		else
			curBranchNum += 1;
	}

	assertPrint(branches.size() == 0, "Assert failed: There are unscheduled branches remaining.\n");

// // {{{ old planner
//   vector<stageSummary*> doneStages;
// 
//   int curKernelID = 0;
//   for(unsigned i=0; i<stages.size(); i++){
// 
//     assignBinSummary& ass = stages[i].assignBin;
//     scheduleSummary&  sch = stages[i].schedules[0];
//     processSummary&   pro = stages[i].process;
// 
//     int nextKernelID = curKernelID+1;
// 
//     ass.kernelID = curKernelID;
// 
//     if(i>0 && !canFuse(stages[i-1],stages[i],0,doneStages)){
//       curKernelID = nextKernelID;
// 
//       doneStages.push_back(&stages[i-1]);
//     }
// 
//     sch.kernelID = curKernelID;
// 
// 	// TODO: Think about what PreScheduleCandiate means, and whether schedAll should be a preschedule candidate
//     if(sch.isPreScheduleCandidate) sch.kernelID = ass.kernelID;
//     else if(sch.schedPolicy != schedLoadBalance
// 			&& sch.schedPolicy != schedAll) curKernelID += 1;
// 
//     pro.kernelID = curKernelID;
// 
//   }
// // }}}

  printf("* Number of Kernels: %d\n", curKernelID);
	printf("|Level ID|\n");

  curKernelID         = -1;
  curBucketLoopLevel  = -1;
  curBucketLoopID  = -1;
  for(unsigned i=0; i<stagesInOrder.size(); i++){

    const stageSummary& stg = *stagesInOrder[i];

    const assignBinSummary& ass = stg.assignBin;
    const scheduleSummary&  sch = stg.schedules[0];
    const processSummary&   pro = stg.process;

    if(ass.policy != assignEmpty){
			curBucketLoopID = ass.bucketLoopID;
			curBucketLoopLevel = ass.bucketLoopLevel;
      if(ass.kernelID != curKernelID) {
        printf("|%d %d| * Kernel %d:\n",curBucketLoopLevel,curBucketLoopID,ass.kernelID);
			}
      curKernelID         = ass.kernelID;
      //printf("|%d-%d|   - [%d] %s.AssignBin\n",curBucketLoopLevel,curBucketLoopID,stg.distFromDrain, stg.name.c_str());
      printf("          - [%d] %s.AssignBin\n",stg.distFromDrain, stg.name.c_str());
    }
    
		curBucketLoopID = sch.bucketLoopID;
    curBucketLoopLevel    = sch.bucketLoopLevel;
    if(sch.kernelID != curKernelID) {
      printf("|%d %d| * Kernel %d:\n",curBucketLoopLevel,curBucketLoopID,sch.kernelID); 
		}
    curKernelID           = sch.kernelID;

    //printf("|%d-%d|   -     %s.Schedule%s",curBucketLoopLevel,curBucketLoopID, stg.name.c_str(), 
    printf("          -     %s.Schedule%s", stg.name.c_str(), 
      sch.isPreScheduleCandidate?       "\t<--- 1 core per block\n":
      sch.schedPolicy==schedLoadBalance? "\t<--- 1 bin per block\n":"\n"); // \t<--- trivialized to cuda scheduler

    if(pro.policy != procEmpty){
			curBucketLoopID = sch.bucketLoopID;
      curBucketLoopLevel  = pro.bucketLoopLevel;
      if(pro.kernelID != curKernelID) {
        printf("|%d %d| * Kernel %d:\n",curBucketLoopLevel,curBucketLoopID,pro.kernelID);
			}
      curKernelID         = pro.kernelID;
      //printf("|%d-%d|   -     %s.Process\n",curBucketLoopLevel,curBucketLoopID, stg.name.c_str());
      printf("          -     %s.Process\n", stg.name.c_str());
    }
  }
  printf("\n");

// commenting out kernel order code because it doesn't work yet
/*
	printf("\tKernel order:\n");
	vector< pair<int,string> > *kernOrder = new vector< pair<int,string> >();
	stages[0].findKernelOrder(-1, 0, kernOrder);

	int curBatch = 0;
	printf("\tBatch %d\n", curBatch);
	for(unsigned int i=0; i<kernOrder->size(); ++i) {
		if((*kernOrder)[i].first == curBatch)
			printf("\t%s", (*kernOrder)[i].second.c_str());
		else {
			curBatch = (*kernOrder)[i].first;
			printf("\tBatch %d\n", curBatch);
			printf("\t%s", (*kernOrder)[i].second.c_str());
		}
	}
*/
	
}

void stageSummary::findKernelOrder(int kernelID, int batch, vector< pair<int,string> > *order) {
	stringstream ss;
	ss.str("");
	ss.clear();
	int curKernelID = kernelID;
	int curBatch = batch;

	assignBinSummary& ass = assignBin;
	scheduleSummary&  sch = schedules[0];
	processSummary&   pro = process;
	
	if(ass.kernelID != curKernelID) {
		if(ass.policy != assignEmpty) {
			curKernelID = ass.kernelID;
			ss << "\tKernel " << curKernelID << endl;
		}
	}

	if(sch.kernelID != curKernelID) {
		curKernelID = sch.kernelID;
		ss << "\tKernel " << curKernelID << endl;
	}

	if(pro.kernelID != curKernelID) {
		if(pro.policy != procEmpty) {
			curKernelID = pro.kernelID;
			ss << "\tKernel " << curKernelID << endl;
		}
	}

//printf("!!! HERE 1 !!!\n");

	pair<int,string> ret(curBatch, ss.str());

//printf("SIZE: %d\n", ret.second.length());
//printf("!!! HERE 2 !!!\n");
	if(ret.second != "") {
//printf("!!! HERE 3 !!!\n");
		order->push_back(ret);
//printf("!!! HERE 4 !!!\n");
		ss.str("");
		ss.clear();
//printf("!!! HERE 5 !!!\n");
	}

	if(nextStages.size() > 1) curBatch += 1;
	for(unsigned int i=0; i<nextStages.size(); ++i) {
//printf("iter = %d\n",i);
//printf("!!! HERE 1 !!!\n");
		if(isCurOrPrevStage(nextStages[i])) {
//printf("!!! HERE 2 !!!\n");
			ss << "repeat kernel " << nextStages[i]->assignBin.kernelID << " to kernel " << curKernelID << " sequence\n";
			
			ret.first = (curBatch == batch) ? curBatch+1 : curBatch;
			ret.second = ss.str();
			order->push_back(ret);
			ss.str("");
			ss.clear();
		}

		else {
//printf("!!! HERE 3 !!!\n");
			if(nextStages[i]->prevStages.size() > 1)
				nextStages[i]->findKernelOrder(curKernelID, curBatch+1, order);
			else if(nextStages[i]->schedules[0].waitPolicy == waitEndStage) 
				nextStages[i]->findKernelOrder(curKernelID, curBatch+1, order);
			else
				nextStages[i]->findKernelOrder(curKernelID, curBatch, order);
		}
	}
}

bool stageSummary::isCurOrPrevStage(stageSummary *stg) {
//printf("stg   = %s\n", stg->name.c_str());
//printf("this  = %s\n", this->name.c_str());
//printf("!!! HERE 1 !!!\n");
	if(stg == this) return true;
//printf("!!! HERE 2 !!!\n");

	for(unsigned int i=0; i<prevStages.size(); ++i) {
//printf("this = %s\n", this->name.c_str());
//printf("prev = %s\n", this->prevStages[i]->name.c_str());
//printf("!!! HERE 3 !!!\n");
		if(prevStages[i]->isCurOrPrevStage(stg)) return true;
	}

//printf("!!! HERE 4 !!!\n");
	return false;
}

// update distances from drain stage (recursively)
void PipeSummary::updateDrainDistance(stageSummary* stage){
  if(stage != NULL){
    for(unsigned i=0; i<stage->prevStages.size(); i++){
      stageSummary* ps = stage->prevStages[i];

      int oldDist = ps->distFromDrain;

      ps->distFromDrain = min(stage->distFromDrain+1, oldDist);

      int newDist = ps->distFromDrain;

      if(oldDist != newDist)
        updateDrainDistance(ps);
    }
  }
}

// update all links
void PipeSummary::processLinks(){
  for(unsigned i=0; i<stages.size(); i++){
    // update nextStages
    for(unsigned j=0; j<stages[i].nextStageNames.size(); j++){
      stageSummary* nextStage = findStageByName(stages[i].nextStageNames[j]);
      if(nextStage!=NULL){
        stages[i].nextStages.push_back(nextStage);
        nextStage->prevStages.push_back(&stages[i]);
      }
    }

    // update endStagePtr if waitPolicy is set to endStage
    if(stages[i].schedules[0].waitPolicy == waitEndStage){
      stages[i].schedules[0].endStagePtr = findStageByName(stages[i].schedules[0].endStageName);
    }
  }
}

// given stage name, fetch pointer
stageSummary* PipeSummary::findStageByName(const string& stageName){
  for(unsigned i=0; i<stages.size(); i++){
    if(stageName == stages[i].name) return &stages[i];
  }
  printf("Cannot find Stage named \"%s\"\n",stageName.c_str());
  return NULL;
}

// given stage type, fetch pointer and return vector
vector<stageSummary*> PipeSummary::findStageByType(const string& stageType){
	vector<stageSummary*> ret;
  for(unsigned i=0; i<stages.size(); i++){
    if(stageType == stages[i].type) ret.push_back(&stages[i]);
  }

	if(ret.size() > 0) return ret;
	else {
		printf("Cannot find Stage type \"%s\"\n",stageType.c_str());
		exit(3);
		return ret;
	}
}

// display pipe summary
void PipeSummary::displaySummary(){

  printf("Pipe %s\n",name.c_str());

  for(unsigned i=0; i<stages.size(); i++){
    stageSummary& curStage = stages[i];

    printf("\tStage %s\n",curStage.name.c_str());

		printf("\t\tType: %s\n", curStage.type.c_str());
    printf("\t\tBinsize: %d x %d\n", curStage.binsize.x(), curStage.binsize.y());
    printf("\t\tNextStages: ");
    for(unsigned j=0; j<curStage.nextStageNames.size(); j++) 
      printf("%s ",curStage.nextStageNames[j].c_str());
    printf("\n");
    printf("\t\tCode:        %s\n",curStage.codeFile.c_str());

    printf("\t\tAssignBin\n");
    printf("\t\t\tCode:        %s\n",curStage.assignBin.codeFile.c_str());
    printf("\t\t\tPolicy:      %s\n",toString(curStage.assignBin.policy).c_str());
		printf("\t\t\ttrivial:     %s\n", (curStage.assignBin.trivial) ? "true" : "false");
    //printf("\t\t\tCode:        %s\n",curStage.assignBin.codeFile.c_str());

    for(unsigned j=0; j<curStage.schedules.size(); j++){
      printf("\t\tSchedule\n");
			printf("\t\t\tCode:        %s\n",curStage.schedules[j].codeFile.c_str());
      printf("\t\t\tArch:            %s\n", toString(curStage.schedules[j].arch).c_str());
      printf("\t\t\tschedPolicy:     %s\n", toString(curStage.schedules[j].schedPolicy).c_str());
      printf("\t\t\ttileSplitSize:   %d\n", curStage.schedules[j].tileSplitSize);
      printf("\t\t\twaitPolicy:      %s\n", toString(curStage.schedules[j].waitPolicy).c_str());
      printf("\t\t\twaitBatchSize:   %d\n", curStage.schedules[j].waitBatchSize);
			printf("\t\t\ttrivial:         %s\n", (curStage.schedules[j].trivial) ? "true" : "false");
      //printf("\t\t\tCode:        %s\n",curStage.schedules[j].codeFile.c_str());
    }

    printf("\t\tProcess\n");
    printf("\t\t\tCode:        %s\n",curStage.process.codeFile.c_str());
    printf("\t\t\tMaxOutPrims: %d\n",curStage.process.maxOutPrims);
		printf("\t\t\ttrivial:     %s\n", (curStage.process.trivial) ? "true" : "false");
    //printf("\t\t\tCode:        %s\n",curStage.process.codeFile.c_str());
  }
  printf("---\n");
}


bool PipeSummary::canFuse(stageSummary& s1, stageSummary& s2, int whichSchedule,
    vector<stageSummary*>& doneStages){

	// Cannot fuse two stages if they are the same type
	if(s1.type == s2.type)
		return false;

  scheduleSummary& sch1 = s1.schedules[whichSchedule];
  scheduleSummary& sch2 = s2.schedules[whichSchedule];

  // make sure architectures match (for your sanity)
  if(sch1.arch != sch2.arch) return false;

  // we will not fuse with a stage that has multiple input paths
  if(s2.prevStages.size() > 1) return false;

	if(s1.nextStages.size() > 1) return false;

  // max_fan_out_per_\process ... removing for now
  //if(s1.process.maxOutPrims > 64 && sch2.waitPolicy!=waitNone) return false;

  // if dependencies are not resolved, we cannot fuse
  if(sch2.endStagePtr!=NULL && std::find(doneStages.begin(), doneStages.end(), sch2.endStagePtr) ==
      doneStages.end()) return false;

  // the following flag will check s1 and s2 run in the same core
  bool sameCore = false;

  sameCore = sameCore;

  // fusing DirectMap scheduler
  sameCore = sameCore || ((sch2.schedPolicy    == sch1.schedPolicy) &&
                          (s2.assignBin.policy == assignInBin) &&
                          (sch1.schedPolicy    == schedDirectMap));
  // fusing Serialize Scheduler
  sameCore = sameCore || ((sch2.schedPolicy    == sch1.schedPolicy) &&
                          (sch1.schedPolicy    == schedSerialize));

  // fusing LoadBalance Scheduler
  sameCore = sameCore || ((sch2.schedPolicy    == sch1.schedPolicy) &&
                          (s2.assignBin.policy == assignInBin) &&
                          (sch1.schedPolicy    == schedLoadBalance));

  bool preferLoadBalance = false; 
  
  preferLoadBalance = preferLoadBalance || (sch2.schedPolicy==schedLoadBalance);

  if(  0//(sch2.waitPolicy == waitNone && s2.process.maxOutPrims<=8 && s2.assignBin.policy != assignCustom)
    || ((s2.binsize == s1.binsize) && sameCore && sch1.tileSplitSize == sch2.tileSplitSize)
    || 0
    )
  {
    return true;
  }
  // fusing schedAll
  else if(sch1.schedPolicy    ==  schedAll    && 
          sch2.schedPolicy    ==  schedAll    && 
          s2.assignBin.policy ==  assignInBin && 
          s1.binsize          ==  s2.binsize  &&
          sch2.waitPolicy     ==  waitNone    &&
          sch1.tileSplitSize  ==  sch2.tileSplitSize)
  {
    return true;
  }
  else
  {
    return false;
  }

}
