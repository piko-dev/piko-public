
COMMON_INCLUDES :=  -I/usr/local/cuda/include -I../../api/include -I../.. -I. -I../rasterPipelineFixPt/EasyBMP -I../rasterPipelineFixPt/sceneParser/ -I../rasterPipelineFixPt/util/ -I../rasterPipelineFixPt/assimp/include -I../rasterPipelineFixPt/bezmesh/ -I./basicTypes -I../rasterPipelineFixPt/common_code/
ASSIMP_LIB_PATH := -L../rasterPipelineFixPt/assimp/lib/
ASSIMP_LIB := -lassimp
OBJS := EasyBMP.o vecs.o sceneParser.o bezmesh.o
CUDA_LIB_PATH = -L/usr/local/cuda-7.0/lib64

all: bin/reyes

cpu: bin/reyes-cpu

bin/reyes: dirs pikocGPU main.cpp $(OBJS)
	g++ -D__PIKOC_HOST__ -o bin/reyes -I. $(COMMON_INCLUDES) main.cpp $(CUDA_LIB_PATH) $(ASSIMP_LIB_PATH) $(ASSIMP_LIB) $(OBJS) -lcuda -lglut -lGLU -lGL

pikocGPU: dummy.cpp split.pikostage dice.pikostage shade.pikostage reyesPipe.h
	../../bin/pikoc $(COMMON_INCLUDES) --numRuns=1 --opt --timer dummy.cpp

bin/reyes-cpu: dirs pikocCPU main.cpp $(OBJS)
	g++ -std=c++11 -D__PIKOC_HOST__ -o bin/reyes-cpu -I. $(COMMON_INCLUDES) main.cpp $(CUDA_LIB_PATH) $(ASSIMP_LIB_PATH) $(ASSIMP_LIB) $(OBJS) -lcuda -lglut -lGLU -lGL

pikocCPU: dummy.cpp split.pikostage dice.pikostage shade.pikostage reyesPipe.h
	../../bin/pikoc --target=CPU $(COMMON_INCLUDES) --numRuns=1 --opt --timer dummy.cpp

EasyBMP.o:
	g++ ../rasterPipelineFixPt/EasyBMP/EasyBMP.cpp  $(COMMON_INCLUDES) -c -o EasyBMP.o

vecs.o:
	g++ ../rasterPipelineFixPt/basicTypes/vecs.cpp $(COMMON_INCLUDES) -c -o vecs.o

sceneParser.o: sceneParser/sceneParser.cpp
	g++ sceneParser/sceneParser.cpp $(COMMON_INCLUDES) $(ASSIMP_LIB_PATH) $(ASSIMP_LIB) -c -o sceneParser.o

bezmesh.o: bezmesh/bezmesh.cpp
	g++ bezmesh/bezmesh.cpp $(COMMON_INCLUDES) $(ASSIMP_LIB_PATH) $(ASSIMP_LIB) -c -o bezmesh.o

dirs:
	@mkdir -p bin

clean:
	rm -f bin/reyes bin/reyes-cpu __pikoDefines.h __pikoCompiledPipe.h __pikoCompiledPipe.ptx $(OBJS)
