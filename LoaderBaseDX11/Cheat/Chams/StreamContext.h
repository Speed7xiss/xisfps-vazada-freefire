#pragma once

#include <windows.h>
#include <cstdint>

// ─── ChamsStream::Context ────────────────────────────────────────────────────
// Layout exato esperado pelo shellcode HookedglDrawElements do ChamsShellCode.
// NÃO alterar ordem, tamanho ou tipos dos campos — os offsets estão hardcoded
// no shellcode. Se precisar atualizar, gere novo shellcode e o novo Context.h
// juntos a partir do projeto ChamsShellCode.
//
// Namespace ChamsStream isola do Context.h "default" (Chams normal), que usa
// o mesmo nome de struct mas com layout menor — misturar as duas structs
// faria o shellcode escrever em campos errados e crashar o BlueStacks.
namespace ChamsStream {

    typedef unsigned int  GLenum;
    typedef unsigned int  GLuint;
    typedef int           GLint;
    typedef int           GLsizei;
    typedef unsigned char GLboolean;
    typedef unsigned int  GLbitfield;
    typedef void          GLvoid;
    typedef float         GLfloat;

} // namespace ChamsStream

// Os símbolos GL_* são macros globais — definir fora do namespace e guardar
// por #ifndef para não colidir com outros headers.
#ifndef GL_TRIANGLES
#  define GL_TRIANGLES                0x0004
#endif
#ifndef GL_BLEND
#  define GL_BLEND                    0x0BE2
#endif
#ifndef GL_SRC_ALPHA
#  define GL_SRC_ALPHA                0x0302
#endif
#ifndef GL_ONE
#  define GL_ONE                      0x0001
#endif
#ifndef GL_CURRENT_PROGRAM
#  define GL_CURRENT_PROGRAM          0x8B8D
#endif
#ifndef GL_TRUE
#  define GL_TRUE                     1
#endif
#ifndef GL_FALSE
#  define GL_FALSE                    0
#endif
#ifndef GL_TEXTURE_2D
#  define GL_TEXTURE_2D               0x0DE1
#endif
#ifndef GL_RGBA
#  define GL_RGBA                     0x1908
#endif
#ifndef GL_UNSIGNED_BYTE
#  define GL_UNSIGNED_BYTE            0x1401
#endif
#ifndef GL_TEXTURE_MIN_FILTER
#  define GL_TEXTURE_MIN_FILTER       0x2801
#endif
#ifndef GL_TEXTURE_MAG_FILTER
#  define GL_TEXTURE_MAG_FILTER       0x2800
#endif
#ifndef GL_NEAREST
#  define GL_NEAREST                  0x2600
#endif
#ifndef GL_FRAMEBUFFER
#  define GL_FRAMEBUFFER              0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#  define GL_COLOR_ATTACHMENT0        0x8CE0
#endif
#ifndef GL_DEPTH_ATTACHMENT
#  define GL_DEPTH_ATTACHMENT         0x8D00
#endif
#ifndef GL_RENDERBUFFER
#  define GL_RENDERBUFFER             0x8D41
#endif
#ifndef GL_DEPTH_COMPONENT24
#  define GL_DEPTH_COMPONENT24        0x81A6
#endif
#ifndef GL_READ_FRAMEBUFFER
#  define GL_READ_FRAMEBUFFER         0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#  define GL_DRAW_FRAMEBUFFER         0x8CA9
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#  define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_DEPTH_BUFFER_BIT
#  define GL_DEPTH_BUFFER_BIT         0x00000100
#endif
#ifndef GL_VIEWPORT
#  define GL_VIEWPORT                 0x0BA2
#endif
#ifndef GL_COLOR_BUFFER_BIT
#  define GL_COLOR_BUFFER_BIT         0x00004000
#endif

namespace ChamsStream {

    typedef void   ( WINAPI* PFN_glDrawElements )( GLenum , GLsizei , GLenum , const GLvoid* );
    typedef void   ( WINAPI* PFN_glGetIntegerv )( GLenum , GLint* );
    typedef void   ( WINAPI* PFN_glEnable )( GLenum );
    typedef void   ( WINAPI* PFN_glDisable )( GLenum );
    typedef void   ( WINAPI* PFN_glColorMask )( GLboolean , GLboolean , GLboolean , GLboolean );
    typedef void   ( WINAPI* PFN_glBlendFunc )( GLenum , GLenum );
    typedef void   ( WINAPI* PFN_glDepthRange )( double , double );
    typedef GLint  ( WINAPI* PFN_glGetUniformLocation )( GLuint , const char* );
    typedef void*  ( WINAPI* PFN_wglGetProcAddress )( const char* );
    typedef SHORT  ( WINAPI* PFN_GetAsyncKeyState )( int );

    typedef void   ( WINAPI* PFN_glGenTextures )( GLsizei , GLuint* );
    typedef void   ( WINAPI* PFN_glBindTexture )( GLenum , GLuint );
    typedef void   ( WINAPI* PFN_glTexImage2D )( GLenum , GLint , GLint , GLsizei , GLsizei , GLint , GLenum , GLenum , const void* );
    typedef void   ( WINAPI* PFN_glTexParameteri )( GLenum , GLenum , GLint );
    typedef void   ( WINAPI* PFN_glReadPixels )( GLint , GLint , GLsizei , GLsizei , GLenum , GLenum , void* );
    typedef void   ( WINAPI* PFN_glClear )( GLbitfield );
    typedef void   ( WINAPI* PFN_glClearColor )( GLfloat , GLfloat , GLfloat , GLfloat );
    typedef void   ( WINAPI* PFN_glViewport )( GLint , GLint , GLsizei , GLsizei );
    typedef void   ( WINAPI* PFN_glDeleteTextures )( GLsizei , const GLuint* );

    typedef void   ( WINAPI* PFN_glGenFramebuffers )( GLsizei , GLuint* );
    typedef void   ( WINAPI* PFN_glBindFramebuffer )( GLenum , GLuint );
    typedef void   ( WINAPI* PFN_glFramebufferTexture2D )( GLenum , GLenum , GLenum , GLuint , GLint );
    typedef void   ( WINAPI* PFN_glDeleteFramebuffers )( GLsizei , const GLuint* );
    typedef void   ( WINAPI* PFN_glGenRenderbuffers )( GLsizei , GLuint* );
    typedef void   ( WINAPI* PFN_glBindRenderbuffer )( GLenum , GLuint );
    typedef void   ( WINAPI* PFN_glRenderbufferStorage )( GLenum , GLenum , GLsizei , GLsizei );
    typedef void   ( WINAPI* PFN_glFramebufferRenderbuffer )( GLenum , GLenum , GLenum , GLuint );
    typedef void   ( WINAPI* PFN_glDeleteRenderbuffers )( GLsizei , const GLuint* );
    typedef void   ( WINAPI* PFN_glBlitFramebuffer )( GLint , GLint , GLint , GLint , GLint , GLint , GLint , GLint , GLbitfield , GLenum );

    typedef DWORD  ( WINAPI* PFN_GetTickCount_t )( void );

    // ─── Stream buffer ──────────────────────────────────────────────────
    static constexpr int CHAMS_STREAM_MAX_W      = 1920;
    static constexpr int CHAMS_STREAM_MAX_H      = 1080;
    static constexpr int CHAMS_STREAM_BPP        = 4;
    static constexpr size_t CHAMS_STREAM_SLOT_BYTES =
        ( size_t ) CHAMS_STREAM_MAX_W * CHAMS_STREAM_MAX_H * CHAMS_STREAM_BPP;

    struct StreamHeader {
        volatile LONG activeSlot;
        volatile LONG width;
        volatile LONG height;
        volatile LONG frameSeq;
    };

    struct Context {
        PFN_glGetIntegerv              pglGetIntegerv;
        PFN_glEnable                   pglEnable;
        PFN_glDisable                  pglDisable;
        PFN_glColorMask                pglColorMask;
        PFN_glBlendFunc                pglBlendFunc;
        PFN_glDepthRange               pglDepthRange;
        PFN_wglGetProcAddress          pwglGetProcAddress;
        PFN_GetAsyncKeyState           pGetAsyncKeyState;
        PFN_glDrawElements             pOriginalDraw;
        PFN_glGetUniformLocation       pglGetUniformLocation;

        PFN_glGenTextures              pglGenTextures;
        PFN_glBindTexture              pglBindTexture;
        PFN_glTexImage2D               pglTexImage2D;
        PFN_glTexParameteri            pglTexParameteri;
        PFN_glReadPixels               pglReadPixels;
        PFN_glClear                    pglClear;
        PFN_glClearColor               pglClearColor;
        PFN_glViewport                 pglViewport;
        PFN_glDeleteTextures           pglDeleteTextures;

        PFN_glGenFramebuffers          pglGenFramebuffers;
        PFN_glBindFramebuffer          pglBindFramebuffer;
        PFN_glFramebufferTexture2D     pglFramebufferTexture2D;
        PFN_glDeleteFramebuffers       pglDeleteFramebuffers;
        PFN_glGenRenderbuffers         pglGenRenderbuffers;
        PFN_glBindRenderbuffer         pglBindRenderbuffer;
        PFN_glRenderbufferStorage      pglRenderbufferStorage;
        PFN_glFramebufferRenderbuffer  pglFramebufferRenderbuffer;
        PFN_glDeleteRenderbuffers      pglDeleteRenderbuffers;
        PFN_glBlitFramebuffer          pglBlitFramebuffer;

        PFN_GetTickCount_t             pGetTickCount;

        char sGetUniLoc              [ 32 ];
        char sCharaLight             [ 32 ];
        char sGenFramebuffers        [ 32 ];
        char sBindFramebuffer        [ 32 ];
        char sFramebufferTexture2D   [ 32 ];
        char sDeleteFramebuffers     [ 32 ];
        char sGenRenderbuffers       [ 32 ];
        char sBindRenderbuffer       [ 32 ];
        char sRenderbufferStorage    [ 32 ];
        char sFramebufferRenderbuffer[ 32 ];
        char sDeleteRenderbuffers    [ 32 ];
        char sBlitFramebuffer        [ 32 ];

        volatile LONG chamsState;
        volatile LONG inDrawHook;
        volatile LONG streamMode;
        volatile LONG fboInit;
        volatile LONG unloadPending;

        GLuint fbo;
        GLuint tex;
        GLuint depthRbo;
        GLint  fboW;
        GLint  fboH;

        DWORD  lastFrameTick;
        void*  pStreamBuf;
    };

    static constexpr uint64_t CTX_MAGIC = 0xC0DEBABEDEADF00DULL;
}
