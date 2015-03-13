
COMMON_INCLUDES :=  -I/usr/local/cuda/include -I../../api/include -I../.. -IbasicTypes/ -IEasyBMP -IsceneParser/ -Iutil/ -Iassimp/include -Ibezmesh/
ASSIMP_LIB_PATH := -Lassimp/lib/
ASSIMP_LIB := -lassimp
OBJS := EasyBMP.o vecs.o sceneParser.o bezmesh.o

all: bin/pikoraster

cpu: bin/pikoraster-cpu

bin/pikoraster: dirs pikocGPU main.cpp $(OBJS) basicTypes/rasterTypes.h
	@echo - making pikoraster
	@g++ -D__PIKOC_HOST__ -o bin/pikoraster -I. $(COMMON_INCLUDES) main.cpp -L/usr/local/cuda/lib $(ASSIMP_LIB_PATH) $(ASSIMP_LIB) $(OBJS) -lcuda -lGL -lGLU -lglut

pikocGPU: dummy.cpp raster.pikostage vertexShader.pikostage rasterPipe.h basicTypes/rasterTypes.h rasterMacros.h
	@echo - making __pikoCompiledPipe.ptx
	@../../bin/pikoc $(COMMON_INCLUDES) --numRuns=10 --opt --timer dummy.cpp

bin/pikoraster-cpu: dirs pikocCPU main.cpp $(OBJS) basicTypes/rasterTypes.h
	@echo - making pikoraster-cpu
	@g++ -std=c++11 -D__PIKOC_HOST__ -o bin/pikoraster-cpu -I. $(COMMON_INCLUDES) main.cpp -L/usr/local/cuda/lib $(ASSIMP_LIB_PATH) $(ASSIMP_LIB) $(OBJS) -lcuda -lGL -lGLU -lglut

pikocCPU: dummy.cpp raster.pikostage vertexShader.pikostage rasterPipe.h basicTypes/rasterTypes.h rasterMacros.h
	@echo - making __pikoCompiledPipe.h for CPU
	@../../bin/pikoc --target=CPU $(COMMON_INCLUDES) --numRuns=10 --opt --timer dummy.cpp

EasyBMP.o: 
	@echo - making EasyBMP.o
	@g++ EasyBMP/EasyBMP.cpp  $(COMMON_INCLUDES) -c -o EasyBMP.o

vecs.o: 
	@echo - making vecs
	@g++ basicTypes/vecs.cpp $(COMMON_INCLUDES) -c -o vecs.o

sceneParser.o: 
	@echo - making sceneParser
	@g++ sceneParser/sceneParser.cpp $(COMMON_INCLUDES) $(ASSIMP_LIB_PATH) $(ASSIMP_LIB) -c -o sceneParser.o

bezmesh.o: 
	@echo - making bezmesh
	@g++ bezmesh/bezmesh.cpp $(COMMON_INCLUDES) $(ASSIMP_LIB_PATH) $(ASSIMP_LIB) -c -o bezmesh.o

dirs:
	@mkdir -p bin
	@mkdir -p obj

clean:
	rm -f bin/pikoraster bin/pikoraster-cpu __pikoDefines.h __pikoCompiledPipe.h __pikoCompiledPipe.ptx $(OBJS)
