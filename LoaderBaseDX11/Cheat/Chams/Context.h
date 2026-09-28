#pragma once

#include <windows.h>
#include <cstdint>

typedef unsigned int  GLenum;
typedef unsigned int  GLuint;
typedef int           GLint;
typedef int           GLsizei;
typedef unsigned char GLboolean;
typedef void          GLvoid;

#ifndef GL_TRIANGLES
#  define GL_TRIANGLES       0x0004
#endif
#ifndef GL_BLEND
#  define GL_BLEND           0x0BE2
#endif
#ifndef GL_SRC_ALPHA
#  define GL_SRC_ALPHA       0x0302
#endif
#ifndef GL_ONE
#  define GL_ONE             0x0001
#endif
#ifndef GL_CURRENT_PROGRAM
#  define GL_CURRENT_PROGRAM 0x8B8D
#endif
#ifndef GL_TRUE
#  define GL_TRUE            1
#endif
#ifndef GL_FALSE
#  define GL_FALSE           0
#endif

typedef void  (WINAPI* PFN_glDrawElements)(GLenum, GLsizei, GLenum, const GLvoid*);
typedef void  (WINAPI* PFN_glGetIntegerv)(GLenum, GLint*);
typedef void  (WINAPI* PFN_glEnable)(GLenum);
typedef void  (WINAPI* PFN_glDisable)(GLenum);
typedef void  (WINAPI* PFN_glColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
typedef void  (WINAPI* PFN_glBlendFunc)(GLenum, GLenum);
typedef void  (WINAPI* PFN_glDepthRange)(double, double);
typedef GLint (WINAPI* PFN_glGetUniformLocation)(GLuint, const char*);
typedef void* (WINAPI* PFN_wglGetProcAddress)(const char*);
typedef SHORT (WINAPI* PFN_GetAsyncKeyState)(int);

struct ChamsContext
{
    PFN_glGetIntegerv        pglGetIntegerv;
    PFN_glEnable             pglEnable;
    PFN_glDisable            pglDisable;
    PFN_glColorMask          pglColorMask;
    PFN_glBlendFunc          pglBlendFunc;
    PFN_glDepthRange         pglDepthRange;
    PFN_wglGetProcAddress    pwglGetProcAddress;
    PFN_GetAsyncKeyState     pGetAsyncKeyState;

    PFN_glDrawElements       pOriginalDraw;
    PFN_glGetUniformLocation pglGetUniformLocation;

    char sGetUniLoc[32];
    char sCharaLight[32];

    volatile LONG chamsState;
    volatile LONG inDrawHook;
};

#define CHAMS_CTX_MAGIC 0xC0DEBABEDEADF00DULL
