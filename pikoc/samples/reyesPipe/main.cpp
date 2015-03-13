
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <string.h>

// #include "__pikoDefines.h"

#include "reyesPipe.h"
#include "__pikoCompiledPipe.h"

#ifdef __PIKOC_HOST__

#include <GL/glut.h>

#include <piko/builtinTypes.h>
#include "host_math.h"
#include "pikoTypes.h"
#include "FPSMeter.h"

// pikoc does not work well with assimp, so it will not be included when pikoc runs
#ifndef __PIKOC__
#include "sceneParser.h"
#endif  // __PIKOC__


using namespace std;

#define PATCH_BUFFER_SIZE 6000

// ----------------------------------------
// function prototypes
// ----------------------------------------
void init(int argc, char* argv[]);
void initScreen(int W, int H);
void initScene();
void initPipe();
void display();
void destroyApp();
void doPerfTest(int n_runs = 10);
void runPipe();
void pipelineTest();
void resetDepthBuffer();
void printDepthBuffer();

// camera helper functions here
void buildProjectionMatrix();
void buildLookAt();

void glhPerspectivef2(float *matrix, float fovyInDegrees, float aspectRatio,
                      float znear, float zfar);
void glhFrustumf2(float *matrix, float left, float right, float bottom, float top,
                  float znear, float zfar);

void loadPatchBuffer(int start, int end);
// ----------------------------------------
// global variables
// ----------------------------------------


// camera angles
float theta, phi, camdist;

#ifndef __PIKOC__
// main scene
scene sMain;
#endif // __PIKOC__

piko_patch* patchBuffer = NULL;

ReyesPipe piko_pipe;

// state
ConstantState pipelineConstantState;
MutableState pipelineMutableState;

int numPatches;

int main(int argc, char* argv[])
{
  glutInit(&argc, argv);
  initScreen(1024, 768);
  initScene();
  initPipe();
  glutDisplayFunc(display);
  // doPerfTest(100);
  atexit(destroyApp);
  glutMainLoop();
}

cvec4f matmultfloat4(float * mvpMat, cvec4f v)
{
  cvec4f outRes;
  (outRes).x = mvpMat[0] * v.x + mvpMat[4] * v.y + mvpMat[8 ] * v.z + mvpMat[12] * v.w;
  (outRes).y = mvpMat[1] * v.x + mvpMat[5] * v.y + mvpMat[9 ] * v.z + mvpMat[13] * v.w;
  (outRes).z = mvpMat[2] * v.x + mvpMat[6] * v.y + mvpMat[10] * v.z + mvpMat[14] * v.w;
  (outRes).w = mvpMat[3] * v.x + mvpMat[7] * v.y + mvpMat[11] * v.z + mvpMat[15] * v.w;
  return outRes;
}


void initScreen(int W, int H){
  #ifndef __PIKOC__
  sMain.cam().W() = W;
  sMain.cam().H() = H;
#endif // __PIKOC__

  pipelineConstantState.screenSizeX = W;
  pipelineConstantState.screenSizeY = H;

  glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
  glutInitWindowSize(W,H);
  glutCreateWindow("Reyes Pipeline");
  glClearColor(0.0f, 0.0f, 0.2f, 1.0f);
}


void display()
{
  // update state

  buildProjectionMatrix();
  resetDepthBuffer();

  printf("running display\n");

  piko_pipe.prepare();
  piko_pipe.run_single();

  unsigned* data =  piko_pipe.pikoScreen.getData();

  glDrawPixels(pipelineConstantState.screenSizeX, pipelineConstantState.screenSizeY, GL_RGBA, GL_UNSIGNED_BYTE, data);
  glutSwapBuffers();

  // for(int i=0; i< pipelineConstantState.screenSizeX * pipelineConstantState.screenSizeY; i++)
  // {
  //   if(data[i] != 0)
  //     printf("%d: %x\n", i, data[i]);
  // }
}

void doPerfTest(int n_runs)
{
  printf("Running perf test...\n");

  buildProjectionMatrix();
  resetDepthBuffer();
  piko_pipe.prepare();
  piko_pipe.run_single();

  Stopwatch mywatch;

  mywatch.Reset();
  for(int run = 0; run < n_runs; run++)
  {
    buildProjectionMatrix();
    resetDepthBuffer();
    piko_pipe.prepare();
  }
  float prepTime = mywatch.GetTime();

  mywatch.Reset();
  for(int run = 0; run < n_runs; run++)
  {
    buildProjectionMatrix();
    resetDepthBuffer();
    piko_pipe.prepare();
    piko_pipe.run_single();
  }
  float fullrunTime = mywatch.GetTime();

  float total_time_to_ms = 1000.0f / (float) n_runs;

  printf("Prep time     = %0.2f ms\n", total_time_to_ms * (prepTime));
  printf("Full run time = %0.2f ms\n", total_time_to_ms * (fullrunTime));
  printf("Raster time   = %0.2f ms\n", total_time_to_ms * (fullrunTime - prepTime));
}

void initScene(){
  // the scene will only be compiled when going through gcc and not pikoc
  // parse scene file
  sceneParser scp;

  int nT, nV, nP;

  buildProjectionMatrix();


  scp.parseFile("../../..", "bezteapot.scene", &sMain);

  printf("Flattening scene assets: "); fflush(stdout);
  sMain.flatten(nT,nV, nP);
  printf("T: %d, V: %d P:%d\n", nT, nV, nP);
  numPatches = nP;

  // create the final matrix
  // FIXME: perhaps this is flipped?
  //HOST::matmult4x4(pipelineConstantState.projMatrix, pipelineConstantState.viewMatrix,pipelineConstantState.viewProjMatrix );
  //HOST::matmult4x4(pipelineConstantState.viewMatrix, pipelineConstantState.projMatrix, pipelineConstantState.viewProjMatrix);

  sMain.cam().display();

}

void initPipe()
{
  // build the state from the scene
  // camera& cam = sMain.cam();
  // pipelineConstantState.camera_eye = cam.eye();
  // pipelineConstantState.camera_target = cam.target();
  // pipelineConstantState.camera_up = cam.up();
  // if(sMain.lights().size() > 0) {
  //   pipelineConstantState.lightPos = sMain.lights()[0].pos();
  //   pipelineConstantState.lightColor = sMain.lights()[0].dif();
  // }
  // else {
  //   // some default light that might suck
  //   pipelineConstantState.lightPos = gencvec3f(1.0,1.0,1.0);
  //   pipelineConstantState.lightColor = gencvec3f(1.0,1.0,1.0);
  // }

  int numLoadPatches = numPatches;
  loadPatchBuffer(0,numLoadPatches);
  resetDepthBuffer();
  piko_pipe.allocate(pipelineConstantState, pipelineMutableState, patchBuffer, numLoadPatches);
}

// void runPipe()
// {
//   int count = 1;
//   ReyesPipe p;
//   p.run(pipelineConstantState,patchBuffer, 1);
// }


void buildProjectionMatrix()
{
  camera& cam = sMain.cam();
  glMatrixMode(GL_PROJECTION);

  glLoadIdentity();
  gluPerspective(cam.fovyDeg(), cam.aspect(), cam.zNear(), cam.zFar());
  glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
  gluLookAt(
      cam.eye().x,    cam.eye().y,    cam.eye().z,
      cam.target().x, cam.target().y, cam.target().z,
      cam.up().x,     cam.up().y,     cam.up().z);

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glGetFloatv(GL_MODELVIEW_MATRIX, pipelineConstantState.viewMatrix);
  glMultMatrixf(pipelineConstantState.viewMatrix);
  glGetFloatv(GL_PROJECTION_MATRIX, pipelineConstantState.viewProjMatrix);
  glPopMatrix();

  // printf("final projection matrix:\n");
  // for(int i=0; i<16; i++) {
  //   if (i%4 ==0) printf("\n");
  //     printf("%f ", pipelineConstantState.viewProjMatrix[i]);
  // } printf("\n");

  //   printf("final modelview matrix:\n");
  // for(int i=0; i<16; i++) {
  //   if (i%4 ==0) printf("\n");
  //     printf("%f ", pipelineConstantState.viewMatrix[i]);
  // } printf("\n");
}



void loadPatchBuffer(int start, int end) {
  // lazy create
  if(patchBuffer == NULL) {
    patchBuffer = new piko_patch[PATCH_BUFFER_SIZE];
  }

  int size = end - start;

  if (size <=0) return;

  int counter = 0;
  printf("\nfetching patches from %d to %d\n", start, end);
  for(int i=start; i<end; i++) {
    for(int j=0; j<16; j++)
    {
      patchBuffer[counter].CP[j] = sMain._flatPatches[i*16+j];
      //printf("flat patch: ");
      //disp4(patchBuffer[counter].CP[j]);
      //disp4(sMain._flatPatches[i*16+j]);
      //printf("\n");
    }
    patchBuffer[counter].numSplits = 0;     // all patches begin with zero splits
    patchBuffer[counter].id = counter;
    patchBuffer[counter].bbmin.x = 99999.0f;
    patchBuffer[counter].bbmin.y = 99999.0f;

    patchBuffer[counter].bbmax.x = -99999.0f;
    patchBuffer[counter].bbmax.y = -99999.0f;
    counter++;
  }
}

void pipelineTest()
{
  // test out parts of the pipeline here
  cvec4f point = patchBuffer[0].CP[0];
  printf("\n\n point:\n");
  disp4(point);
  cvec4f clipPoint = matmultfloat4(pipelineConstantState.viewProjMatrix, point);

  if(clipPoint.w == 0.0f) clipPoint.w = 1.0f;

            clipPoint.x /= clipPoint.w;
            clipPoint.y /= clipPoint.w;
            clipPoint.z /= clipPoint.w;

            clipPoint.x = (clipPoint.x+1.0) * 0.5 * pipelineConstantState.screenSizeX;
            clipPoint.y = (clipPoint.y+1.0) * 0.5 * pipelineConstantState.screenSizeY;
    printf("\n");
    disp4(clipPoint);
    printf("\n");
}

void resetDepthBuffer() {
  int nPixels = pipelineConstantState.screenSizeX * pipelineConstantState.screenSizeY;
  for(int i = 0; i < nPixels; i++) {
    pipelineMutableState.zBuffer[i] = 1.0f;
  }
}

void printDepthBuffer() {
  int nPixels = pipelineConstantState.screenSizeX * pipelineConstantState.screenSizeY;
  for(int i = 0; i < nPixels; i++) {
    printf("%f\n", pipelineMutableState.zBuffer[i]);
  }
}

void destroyApp()
{
  piko_pipe.destroy();
}

#endif // __PIKOC_HOST__
