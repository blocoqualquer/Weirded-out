#ifndef PSP_STUB_H
#define PSP_STUB_H
#include <stdlib.h>
#include <stdint.h>
typedef unsigned int SceSize;
typedef struct { float x, y, z, w; } ScePspFVector4;
typedef struct { ScePspFVector4 x, y, z, w; } ScePspFMatrix4;
typedef struct { unsigned int TimeStamp; unsigned int Buttons; unsigned char Lx, Ly; unsigned char Rsrv[6]; } SceCtrlData;
#define PSP_MODULE_INFO(...)
#define PSP_MAIN_THREAD_ATTR(x)
#define THREAD_ATTR_USER 0
#define THREAD_ATTR_VFPU 0
#define GU_COLOR_8888 1
#define GU_VERTEX_32BITF 2
#define GU_TRANSFORM_3D 0
#define GU_TRANSFORM_2D 4
#define GU_TEXTURE_32BITF 8
#define GU_TRIANGLES 3
#define GU_SPRITES 6
#define GU_PROJECTION 0
#define GU_VIEW 1
#define GU_MODEL 2
#define GU_PSM_8888 3
#define GU_DIRECT 1
#define GU_SCISSOR_TEST 1
#define GU_GEQUAL 1
#define GU_DEPTH_TEST 2
#define GU_CULL_FACE 3
#define GU_SMOOTH 1
#define GU_CLIP_PLANES 4
#define GU_TRUE 1
#define GU_COLOR_BUFFER_BIT 1
#define GU_DEPTH_BUFFER_BIT 2
#define GU_TEXTURE_2D 5
#define GU_BLEND 6
#define GU_ADD 0
#define GU_SRC_ALPHA 1
#define GU_ONE_MINUS_SRC_ALPHA 2
#define GU_TFX_MODULATE 0
#define GU_TCC_RGBA 1
#define GU_NEAREST 0
#define GU_CLAMP 0
#define PSP_CTRL_UP 0x10
#define PSP_CTRL_RIGHT 0x20
#define PSP_CTRL_DOWN 0x40
#define PSP_CTRL_LEFT 0x80
#define PSP_CTRL_TRIANGLE 0x1000
#define PSP_CTRL_CROSS 0x4000
#define PSP_CTRL_SQUARE 0x8000
#define PSP_CTRL_MODE_ANALOG 1
extern long g_draw_calls, g_vtx;
static inline int sceKernelCreateCallback(const char*n,int(*f)(int,int,void*),void*c){(void)n;(void)f;(void)c;return 1;}
static inline void sceKernelRegisterExitCallback(int c){(void)c;}
static inline void sceKernelSleepThreadCB(void){}
static inline int sceKernelCreateThread(const char*n,int(*f)(SceSize,void*),int a,int b,int c,void*d){(void)n;(void)f;(void)a;(void)b;(void)c;(void)d;return 1;}
static inline void sceKernelStartThread(int t,int a,void*b){(void)t;(void)a;(void)b;}
static inline void sceKernelExitGame(void){}
static inline unsigned sceKernelGetSystemTimeLow(void){ static unsigned t=12345; t=t*1664525u+1013904223u; return t; }
static inline void sceKernelDcacheWritebackAll(void){}
static inline void sceGuInit(void){}
static inline void sceGuStart(int a,void*b){(void)a;(void)b;}
static inline void sceGuDrawBuffer(int a,void*b,int c){(void)a;(void)b;(void)c;}
static inline void sceGuDispBuffer(int a,int b,void*c,int d){(void)a;(void)b;(void)c;(void)d;}
static inline void sceGuDepthBuffer(void*a,int b){(void)a;(void)b;}
static inline void sceGuOffset(int a,int b){(void)a;(void)b;}
static inline void sceGuViewport(int a,int b,int c,int d){(void)a;(void)b;(void)c;(void)d;}
static inline void sceGuDepthRange(int a,int b){(void)a;(void)b;}
static inline void sceGuScissor(int a,int b,int c,int d){(void)a;(void)b;(void)c;(void)d;}
static inline void sceGuEnable(int a){(void)a;}
static inline void sceGuDisable(int a){(void)a;}
static inline void sceGuDepthFunc(int a){(void)a;}
static inline void sceGuShadeModel(int a){(void)a;}
static inline void sceGuFinish(void){}
static inline void sceGuSync(int a,int b){(void)a;(void)b;}
static inline void sceGuDisplay(int a){(void)a;}
static inline void sceGuTerm(void){}
static inline void sceGuClearColor(unsigned a){(void)a;}
static inline void sceGuClearDepth(unsigned a){(void)a;}
static inline void sceGuClear(int a){(void)a;}
static inline void sceGuBlendFunc(int a,int b,int c,unsigned d,unsigned e){(void)a;(void)b;(void)c;(void)d;(void)e;}
static inline void sceGuTexMode(int a,int b,int c,int d){(void)a;(void)b;(void)c;(void)d;}
static inline void sceGuTexImage(int a,int b,int c,int d,const void*e){(void)a;(void)b;(void)c;(void)d;(void)e;}
static inline void sceGuTexFunc(int a,int b){(void)a;(void)b;}
static inline void sceGuTexFilter(int a,int b){(void)a;(void)b;}
static inline void sceGuTexWrap(int a,int b){(void)a;(void)b;}
static inline void sceGuSwapBuffers(void){}
static inline void sceDisplayWaitVblankStart(void){}
static inline void sceCtrlSetSamplingCycle(int a){(void)a;}
static inline void sceCtrlSetSamplingMode(int a){(void)a;}
static inline void sceCtrlPeekBufferPositive(SceCtrlData*p,int n){(void)p;(void)n;}
/* sceGuGetMemory: aloca de verdade (ASan pega acessos fora do limite) */
static inline void* sceGuGetMemory(int size){ return malloc((size_t)size); }
/* sceGuDrawArray: valida que os indices/contagens fazem sentido e le todos os vertices */
static inline void sceGuDrawArray(int prim,int vtype,int count,const void*idx,const void*v){
  (void)idx; g_draw_calls++; g_vtx+=count;
  if(!v||count<=0) abort();
  volatile unsigned char s=0; size_t stride = (vtype & GU_TRANSFORM_2D) ? 24 : 16;
  const unsigned char*p=(const unsigned char*)v; for(size_t i=0;i<(size_t)count*stride;i++) s^=p[i];
  (void)prim;(void)s;
}
static inline void sceGuSetMatrix(int t,const ScePspFMatrix4*m){(void)t;(void)m;}
#endif
