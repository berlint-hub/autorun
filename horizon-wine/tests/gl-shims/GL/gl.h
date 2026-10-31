/* The OpenGL names source/compositor_gl.h draws with, for the host check that
 * drives it against a table of counting stubs (tests/compositor_gl_calls.c).
 *
 * A machine with real GL headers uses those: the check puts this directory on
 * the include path first, and nothing else in the tree does. The values are
 * the ones GL gives them, so a mixed-up constant in the check would be a real
 * mix-up; no call here reaches a driver. */
#ifndef WINE_NX_GL_SHIM_H
#define WINE_NX_GL_SHIM_H

#include <stddef.h>

typedef unsigned int   GLenum;
typedef unsigned char  GLboolean;
typedef unsigned int   GLbitfield;
typedef signed char    GLbyte;
typedef short          GLshort;
typedef int            GLint;
typedef int            GLsizei;
typedef unsigned char  GLubyte;
typedef unsigned short GLushort;
typedef unsigned int   GLuint;
typedef float          GLfloat;
typedef float          GLclampf;
typedef double         GLdouble;
typedef char           GLchar;
typedef ptrdiff_t      GLsizeiptr;
typedef ptrdiff_t      GLintptr;
typedef void           GLvoid;

#define GL_FALSE 0
#define GL_TRUE  1

#define GL_NO_ERROR 0

#define GL_TRIANGLE_STRIP 0x0005
#define GL_FLOAT          0x1406
#define GL_UNSIGNED_BYTE  0x1401

#define GL_COLOR_BUFFER_BIT 0x00004000

#define GL_BLEND              0x0BE2
#define GL_SRC_ALPHA          0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303

#define GL_TEXTURE_2D          0x0DE1
#define GL_TEXTURE0            0x84C0
#define GL_TEXTURE_MAG_FILTER  0x2800
#define GL_TEXTURE_MIN_FILTER  0x2801
#define GL_TEXTURE_WRAP_S      0x2802
#define GL_TEXTURE_WRAP_T      0x2803
#define GL_TEXTURE_MAX_LEVEL   0x813D
#define GL_NEAREST             0x2600
#define GL_CLAMP_TO_EDGE       0x812F

#define GL_RGBA  0x1908
#define GL_RGBA8 0x8058

#define GL_UNPACK_ALIGNMENT  0x0CF5
#define GL_UNPACK_ROW_LENGTH 0x0D02

#define GL_ARRAY_BUFFER   0x8892
#define GL_STATIC_DRAW    0x88E4

#define GL_VERTEX_SHADER   0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS  0x8B81
#define GL_LINK_STATUS     0x8B82

#endif /* WINE_NX_GL_SHIM_H */
