/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
/*
 * Post-processing of an OpenGL program's frame at its swap: anti-aliasing
 * (FXAA) and AMD FidelityFX FSR 1, its upscaling (EASU) and its sharpening
 * (RCAS), drawn over the back buffer before it is shown, with the program's
 * own context current. win32u calls it where it draws its overlays
 * (winnx_opengl.c, nx_osk_draw_gl): on nxvk at the swap made on Mesa's GL
 * thread, after the frame's commands.
 *
 * The program's picture is the back buffer's bottom-left corner: all of it,
 * or for EASU a smaller one, where a program in a display mode below the
 * screen's draws on a surface kept at the screen's size. It is copied into a
 * texture of its size, and each pass draws a triangle over its target: FXAA
 * at the picture's size, EASU from it over the whole surface, RCAS over the
 * whole surface, the last of them into the back buffer and each before it
 * into a texture. Everything the passes set is put back as the program left
 * it: what is bound, enabled and masked, the viewport and the clip origin. No
 * GL error is raised for the program to find, and none of its errors is
 * taken from it.
 *
 * Self-contained, without GL headers, so that tests/gl_postfx.c runs it over
 * Mesa on the host. FXAA follows Timothy Lottes' FXAA 3.11 quality algorithm
 * (preset 12); EASU and RCAS follow tools/fsr1/ffx_fsr1.h (FsrEasuF and
 * FsrRcasF, MIT), as the Vulkan presentation's tools/fshack_*.comp do.
 */
#ifndef WINE_NX_GL_POSTFX_H
#define WINE_NX_GL_POSTFX_H

#include <math.h>
#include <stdio.h>
#include <string.h>

enum gl_postfx_aa { GL_POSTFX_AA_OFF, GL_POSTFX_AA_FXAA, GL_POSTFX_AA_COUNT };

/* The GL entry points the passes use; BindSampler and ClipControl may be NULL. */
struct gl_postfx_funcs
{
    void (*GetIntegerv)( unsigned int pname, int *data );
    void (*GetBooleanv)( unsigned int pname, unsigned char *data );
    unsigned int (*GetError)( void );
    unsigned char (*IsEnabled)( unsigned int cap );
    void (*Enable)( unsigned int cap );
    void (*Disable)( unsigned int cap );
    void (*Viewport)( int x, int y, int width, int height );
    void (*ColorMask)( unsigned char r, unsigned char g, unsigned char b, unsigned char a );
    void (*ReadBuffer)( unsigned int mode );
    void (*BindTexture)( unsigned int target, unsigned int texture );
    void (*GenTextures)( int n, unsigned int *textures );
    unsigned char (*IsTexture)( unsigned int texture );
    void (*TexImage2D)( unsigned int target, int level, int internalformat, int width, int height, int border,
                        unsigned int format, unsigned int type, const void *pixels );
    void (*TexParameteri)( unsigned int target, unsigned int pname, int param );
    void (*DrawArrays)( unsigned int mode, int first, int count );
    void (*BindBuffer)( unsigned int target, unsigned int buffer );
    void (*ActiveTexture)( unsigned int texture );
    void (*UseProgram)( unsigned int program );
    unsigned int (*CreateShader)( unsigned int type );
    void (*ShaderSource)( unsigned int shader, int count, const char *const *string, const int *length );
    void (*CompileShader)( unsigned int shader );
    void (*GetShaderiv)( unsigned int shader, unsigned int pname, int *params );
    void (*GetShaderInfoLog)( unsigned int shader, int size, int *length, char *log );
    void (*DeleteShader)( unsigned int shader );
    unsigned int (*CreateProgram)( void );
    void (*AttachShader)( unsigned int program, unsigned int shader );
    void (*LinkProgram)( unsigned int program );
    void (*GetProgramiv)( unsigned int program, unsigned int pname, int *params );
    void (*DeleteProgram)( unsigned int program );
    unsigned char (*IsProgram)( unsigned int program );
    int (*GetUniformLocation)( unsigned int program, const char *name );
    void (*Uniform1i)( int location, int value );
    void (*Uniform1f)( int location, float value );
    void (*Uniform2f)( int location, float x, float y );
    void (*GenVertexArrays)( int n, unsigned int *arrays );
    void (*BindVertexArray)( unsigned int array );
    unsigned char (*IsVertexArray)( unsigned int array );
    void (*GenFramebuffers)( int n, unsigned int *framebuffers );
    void (*BindFramebuffer)( unsigned int target, unsigned int framebuffer );
    void (*FramebufferTexture2D)( unsigned int target, unsigned int attachment, unsigned int textarget,
                                  unsigned int texture, int level );
    unsigned char (*IsFramebuffer)( unsigned int framebuffer );
    void (*BlitFramebuffer)( int sx0, int sy0, int sx1, int sy1, int dx0, int dy0, int dx1, int dy1,
                             unsigned int mask, unsigned int filter );
    void (*BindSampler)( unsigned int unit, unsigned int sampler );
    void (*ClipControl)( unsigned int origin, unsigned int depth );
};

/* What one context holds for the passes: its own names, made in it. */
struct gl_postfx
{
    int failed;                     /* the passes could not be built here */
    int width, height;              /* of the picture's copy */
    int mid_width, mid_height;      /* of FXAA's result */
    int up_width, up_height;        /* of EASU's, for RCAS */
    unsigned int scene, mid, up;    /* the picture, FXAA's result, and EASU's */
    unsigned int scene_fb, mid_fb, up_fb;
    unsigned int array;
    unsigned int fxaa, easu, rcas;  /* programs */
    int fxaa_rcp, easu_scale, easu_offset, rcas_sharpness;  /* their uniforms */
};

#define GLPFX_TEXTURE_2D                0x0DE1
#define GLPFX_TEXTURE0                  0x84C0
#define GLPFX_TEXTURE_BINDING_2D        0x8069
#define GLPFX_ACTIVE_TEXTURE            0x84E0
#define GLPFX_SAMPLER_BINDING           0x8919
#define GLPFX_CURRENT_PROGRAM           0x8B8D
#define GLPFX_VERTEX_ARRAY_BINDING      0x85B5
#define GLPFX_READ_FRAMEBUFFER          0x8CA8
#define GLPFX_DRAW_FRAMEBUFFER          0x8CA9
#define GLPFX_READ_FRAMEBUFFER_BINDING  0x8CAA
#define GLPFX_DRAW_FRAMEBUFFER_BINDING  0x8CA6
#define GLPFX_READ_BUFFER               0x0C02
#define GLPFX_BACK                      0x0405
#define GLPFX_COLOR_ATTACHMENT0         0x8CE0
#define GLPFX_PIXEL_UNPACK_BUFFER       0x88EC
#define GLPFX_PIXEL_UNPACK_BUFFER_BINDING 0x88EF
#define GLPFX_VIEWPORT                  0x0BA2
#define GLPFX_COLOR_WRITEMASK           0x0C23
#define GLPFX_BLEND                     0x0BE2
#define GLPFX_DEPTH_TEST                0x0B71
#define GLPFX_STENCIL_TEST              0x0B90
#define GLPFX_CULL_FACE                 0x0B44
#define GLPFX_SCISSOR_TEST              0x0C11
#define GLPFX_RASTERIZER_DISCARD        0x8C89
#define GLPFX_FRAMEBUFFER_SRGB          0x8DB9
#define GLPFX_COLOR_LOGIC_OP            0x0BF2
#define GLPFX_CLIP_ORIGIN               0x935C
#define GLPFX_CLIP_DEPTH_MODE           0x935D
#define GLPFX_LOWER_LEFT                0x8CA1
#define GLPFX_UPPER_LEFT                0x8CA2
#define GLPFX_RGBA8                     0x8058
#define GLPFX_RGBA                      0x1908
#define GLPFX_UNSIGNED_BYTE             0x1401
#define GLPFX_TEXTURE_MIN_FILTER        0x2801
#define GLPFX_TEXTURE_MAG_FILTER        0x2800
#define GLPFX_TEXTURE_WRAP_S            0x2802
#define GLPFX_TEXTURE_WRAP_T            0x2803
#define GLPFX_LINEAR                    0x2601
#define GLPFX_NEAREST                   0x2600
#define GLPFX_CLAMP_TO_EDGE             0x812F
#define GLPFX_COLOR_BUFFER_BIT          0x00004000
#define GLPFX_TRIANGLES                 0x0004
#define GLPFX_VERTEX_SHADER             0x8B31
#define GLPFX_FRAGMENT_SHADER           0x8B30
#define GLPFX_COMPILE_STATUS            0x8B81
#define GLPFX_LINK_STATUS               0x8B82
#define GLPFX_PASS_UNITS                1

/* A triangle over the whole viewport, and where in the frame each pixel is:
 * (0,0) its bottom left, as the copy of the back buffer has it. */
static const char gl_postfx_vertex[] =
    "#version 130\n"
    "out vec2 uv;\n"
    "void main()\n"
    "{\n"
    "    vec2 c = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
    "    uv = c;\n"
    "    gl_Position = vec4(c * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/* FXAA 3.11, quality preset 12: five steps along an edge (1, 1.5, 2, 4, 12
 * pixels), luma from the colour, as the back buffer's alpha holds none. */
static const char gl_postfx_fxaa[] =
    "#version 130\n"
    "uniform sampler2D frame;\n"
    "uniform vec2 rcp;\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "float luma(vec2 p) { return dot(textureLod(frame, p, 0.0).rgb, vec3(0.299, 0.587, 0.114)); }\n"
    "#define LUMA_AT(x, y) dot(textureLodOffset(frame, uv, 0.0, ivec2(x, y)).rgb, vec3(0.299, 0.587, 0.114))\n"
    "void main()\n"
    "{\n"
    "    const float steps[5] = float[5](1.0, 1.5, 2.0, 4.0, 12.0);\n"
    "    vec4 rgbM = textureLod(frame, uv, 0.0);\n"
    "    float lumaM = dot(rgbM.rgb, vec3(0.299, 0.587, 0.114));\n"
    "    float lumaS = LUMA_AT(0, 1), lumaE = LUMA_AT(1, 0);\n"
    "    float lumaN = LUMA_AT(0, -1), lumaW = LUMA_AT(-1, 0);\n"
    "    float rangeMax = max(max(lumaN, lumaW), max(lumaE, max(lumaS, lumaM)));\n"
    "    float rangeMin = min(min(lumaN, lumaW), min(lumaE, min(lumaS, lumaM)));\n"
    "    float range = rangeMax - rangeMin;\n"
    "    if (range < max(0.0833, rangeMax * 0.166)) { color = vec4(rgbM.rgb, 1.0); return; }\n"
    "    float lumaNW = LUMA_AT(-1, -1), lumaSE = LUMA_AT(1, 1);\n"
    "    float lumaNE = LUMA_AT(1, -1), lumaSW = LUMA_AT(-1, 1);\n"
    "    float lumaNS = lumaN + lumaS, lumaWE = lumaW + lumaE;\n"
    "    float subpixRcpRange = 1.0 / range;\n"
    "    float subpixNSWE = lumaNS + lumaWE;\n"
    "    float edgeHorz1 = -2.0 * lumaM + lumaNS, edgeVert1 = -2.0 * lumaM + lumaWE;\n"
    "    float lumaNESE = lumaNE + lumaSE, lumaNWNE = lumaNW + lumaNE;\n"
    "    float edgeHorz2 = -2.0 * lumaE + lumaNESE, edgeVert2 = -2.0 * lumaN + lumaNWNE;\n"
    "    float lumaNWSW = lumaNW + lumaSW, lumaSWSE = lumaSW + lumaSE;\n"
    "    float edgeHorz4 = abs(edgeHorz1) * 2.0 + abs(edgeHorz2);\n"
    "    float edgeVert4 = abs(edgeVert1) * 2.0 + abs(edgeVert2);\n"
    "    float edgeHorz3 = -2.0 * lumaW + lumaNWSW, edgeVert3 = -2.0 * lumaS + lumaSWSE;\n"
    "    float edgeHorz = abs(edgeHorz3) + edgeHorz4, edgeVert = abs(edgeVert3) + edgeVert4;\n"
    "    float subpixNWSWNESE = lumaNWSW + lumaNESE;\n"
    "    bool horzSpan = edgeHorz >= edgeVert;\n"
    "    float lengthSign = horzSpan ? rcp.y : rcp.x;\n"
    "    float subpixA = subpixNSWE * 2.0 + subpixNWSWNESE;\n"
    "    if (!horzSpan) { lumaN = lumaW; lumaS = lumaE; }\n"
    "    float subpixB = subpixA * (1.0 / 12.0) - lumaM;\n"
    "    float gradientN = lumaN - lumaM, gradientS = lumaS - lumaM;\n"
    "    float lumaNN = lumaN + lumaM, lumaSS = lumaS + lumaM;\n"
    "    bool pairN = abs(gradientN) >= abs(gradientS);\n"
    "    float gradient = max(abs(gradientN), abs(gradientS));\n"
    "    if (pairN) lengthSign = -lengthSign;\n"
    "    float subpixC = clamp(abs(subpixB) * subpixRcpRange, 0.0, 1.0);\n"
    "    vec2 posB = uv;\n"
    "    vec2 offNP = horzSpan ? vec2(rcp.x, 0.0) : vec2(0.0, rcp.y);\n"
    "    if (horzSpan) posB.y += lengthSign * 0.5; else posB.x += lengthSign * 0.5;\n"
    "    vec2 posN = posB - offNP * steps[0], posP = posB + offNP * steps[0];\n"
    "    float subpixD = -2.0 * subpixC + 3.0, subpixE = subpixC * subpixC;\n"
    "    if (!pairN) lumaNN = lumaSS;\n"
    "    float gradientScaled = gradient * 0.25;\n"
    "    float lumaMM = lumaM - lumaNN * 0.5;\n"
    "    float subpixF = subpixD * subpixE;\n"
    "    bool lumaMLTZero = lumaMM < 0.0;\n"
    "    float lumaEndN = luma(posN) - lumaNN * 0.5, lumaEndP = luma(posP) - lumaNN * 0.5;\n"
    "    bool doneN = abs(lumaEndN) >= gradientScaled, doneP = abs(lumaEndP) >= gradientScaled;\n"
    "    for (int i = 1; i < 5 && !(doneN && doneP); i++)\n"
    "    {\n"
    "        if (!doneN) posN -= offNP * steps[i];\n"
    "        if (!doneP) posP += offNP * steps[i];\n"
    "        if (!doneN) { lumaEndN = luma(posN) - lumaNN * 0.5; doneN = abs(lumaEndN) >= gradientScaled; }\n"
    "        if (!doneP) { lumaEndP = luma(posP) - lumaNN * 0.5; doneP = abs(lumaEndP) >= gradientScaled; }\n"
    "    }\n"
    "    float dstN = horzSpan ? uv.x - posN.x : uv.y - posN.y;\n"
    "    float dstP = horzSpan ? posP.x - uv.x : posP.y - uv.y;\n"
    "    bool goodSpanN = (lumaEndN < 0.0) != lumaMLTZero, goodSpanP = (lumaEndP < 0.0) != lumaMLTZero;\n"
    "    float spanLengthRcp = 1.0 / (dstP + dstN);\n"
    "    bool directionN = dstN < dstP;\n"
    "    float dst = min(dstN, dstP);\n"
    "    bool goodSpan = directionN ? goodSpanN : goodSpanP;\n"
    "    float subpixG = subpixF * subpixF;\n"
    "    float pixelOffset = dst * -spanLengthRcp + 0.5;\n"
    "    float subpixH = subpixG * 0.75;\n"
    "    float pixelOffsetSubpix = max(goodSpan ? pixelOffset : 0.0, subpixH);\n"
    "    vec2 pos = uv;\n"
    "    if (horzSpan) pos.y += pixelOffsetSubpix * lengthSign; else pos.x += pixelOffsetSubpix * lengthSign;\n"
    "    color = vec4(textureLod(frame, pos, 0.0).rgb, 1.0);\n"
    "}\n";

/* FSR 1's EASU: each pixel of the surface from the 12 texels around where it
 * falls in the picture, a Lanczos-like kernel stretched along the edge the
 * picture's luma has there, the result held within the nearest four. scale
 * is the picture's size over the surface's, offset half of it less half a
 * texel (FsrEasuCon's con0). The texels are read one by one, clamped to the
 * picture, where FSR gathers them; the reciprocals FSR approximates are
 * exact. */
static const char gl_postfx_easu[] =
    "#version 130\n"
    "uniform sampler2D frame;\n"
    "uniform vec2 scale, offset;\n"
    "out vec4 color;\n"
    "ivec2 last;\n"
    "vec3 at(ivec2 p) { return texelFetch(frame, clamp(p, ivec2(0), last), 0).rgb; }\n"
    "float luma(vec3 c) { return c.b * 0.5 + (c.r * 0.5 + c.g); }\n"
    "void edge(inout vec2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE)\n"
    "{\n"
    "    float lenX = 1.0 / max(max(abs(lD - lC), abs(lC - lB)), 1.0 / 32768.0);\n"
    "    float dirX = lD - lB;\n"
    "    dir.x += dirX * w;\n"
    "    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);\n"
    "    len += lenX * lenX * w;\n"
    "    float lenY = 1.0 / max(max(abs(lE - lC), abs(lC - lA)), 1.0 / 32768.0);\n"
    "    float dirY = lE - lA;\n"
    "    dir.y += dirY * w;\n"
    "    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);\n"
    "    len += lenY * lenY * w;\n"
    "}\n"
    "void tap(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c)\n"
    "{\n"
    "    vec2 v = vec2(off.x * dir.x + off.y * dir.y, off.y * dir.x - off.x * dir.y) * len;\n"
    "    float d2 = min(dot(v, v), clp);\n"
    "    float wB = 2.0 / 5.0 * d2 - 1.0, wA = lob * d2 - 1.0;\n"
    "    wB *= wB;\n"
    "    wA *= wA;\n"
    "    wB = 25.0 / 16.0 * wB - (25.0 / 16.0 - 1.0);\n"
    "    aC += c * (wB * wA);\n"
    "    aW += wB * wA;\n"
    "}\n"
    "void main()\n"
    "{\n"
    "    last = textureSize(frame, 0) - 1;\n"
    "    vec2 pp = floor(gl_FragCoord.xy) * scale + offset, fp = floor(pp);\n"
    "    ivec2 q = ivec2(fp);\n"
    "    pp -= fp;\n"
    "    vec3 b = at(q + ivec2(0, -1)), c = at(q + ivec2(1, -1));\n"
    "    vec3 e = at(q + ivec2(-1, 0)), f = at(q), g = at(q + ivec2(1, 0)), h = at(q + ivec2(2, 0));\n"
    "    vec3 i = at(q + ivec2(-1, 1)), j = at(q + ivec2(0, 1)), k = at(q + ivec2(1, 1)), l = at(q + ivec2(2, 1));\n"
    "    vec3 n = at(q + ivec2(0, 2)), o = at(q + ivec2(1, 2));\n"
    "    float bL = luma(b), cL = luma(c), eL = luma(e), fL = luma(f), gL = luma(g), hL = luma(h);\n"
    "    float iL = luma(i), jL = luma(j), kL = luma(k), lL = luma(l), nL = luma(n), oL = luma(o);\n"
    "    vec2 dir = vec2(0.0);\n"
    "    float len = 0.0;\n"
    "    edge(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);\n"
    "    edge(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);\n"
    "    edge(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);\n"
    "    edge(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);\n"
    "    float dirR = dot(dir, dir);\n"
    "    bool zero = dirR < 1.0 / 32768.0;\n"
    "    dirR = zero ? 1.0 : inversesqrt(dirR);\n"
    "    if (zero) dir.x = 1.0;\n"
    "    dir *= dirR;\n"
    "    len *= 0.5;\n"
    "    len *= len;\n"
    "    float stretch = dot(dir, dir) / max(abs(dir.x), abs(dir.y));\n"
    "    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);\n"
    "    float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len, clp = 1.0 / lob;\n"
    "    vec3 min4 = min(min(f, g), min(j, k)), max4 = max(max(f, g), max(j, k));\n"
    "    vec3 aC = vec3(0.0);\n"
    "    float aW = 0.0;\n"
    "    tap(aC, aW, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, b);\n"
    "    tap(aC, aW, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, c);\n"
    "    tap(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, i);\n"
    "    tap(aC, aW, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, j);\n"
    "    tap(aC, aW, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, f);\n"
    "    tap(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, e);\n"
    "    tap(aC, aW, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, k);\n"
    "    tap(aC, aW, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, l);\n"
    "    tap(aC, aW, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, h);\n"
    "    tap(aC, aW, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, g);\n"
    "    tap(aC, aW, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, o);\n"
    "    tap(aC, aW, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, n);\n"
    "    color = vec4(min(max4, max(min4, aC / aW)), 1.0);\n"
    "}\n";

/* FSR 1's RCAS: the four neighbours' range limits how far the centre may be
 * pushed from them, so it sharpens without ringing. sharpness is FSR's con.x,
 * 2 to the minus the stops below the strongest. Reads clamp to the frame. */
static const char gl_postfx_rcas[] =
    "#version 130\n"
    "uniform sampler2D frame;\n"
    "uniform float sharpness;\n"
    "out vec4 color;\n"
    "vec3 at(ivec2 p, ivec2 last) { return texelFetch(frame, clamp(p, ivec2(0), last), 0).rgb; }\n"
    "void main()\n"
    "{\n"
    "    ivec2 p = ivec2(gl_FragCoord.xy), last = textureSize(frame, 0) - 1;\n"
    "    vec3 b = at(p + ivec2(0, 1), last), d = at(p + ivec2(-1, 0), last), e = at(p, last);\n"
    "    vec3 f = at(p + ivec2(1, 0), last), h = at(p + ivec2(0, -1), last);\n"
    "    vec3 mn4 = min(min(b, d), min(f, h)), mx4 = max(max(b, d), max(f, h));\n"
    "    vec3 hitMin = min(mn4, e) / max(4.0 * mx4, vec3(1.0 / 65536.0));\n"
    "    vec3 hitMax = (1.0 - max(mx4, e)) / min(4.0 * mn4 - 4.0, vec3(-1.0 / 65536.0));\n"
    "    vec3 lobe3 = max(-hitMin, hitMax);\n"
    "    float lobe = max(-(0.25 - 1.0 / 16.0), min(max(lobe3.r, max(lobe3.g, lobe3.b)), 0.0)) * sharpness;\n"
    "    color = vec4((lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0), 1.0);\n"
    "}\n";

static int gl_postfx_load( struct gl_postfx_funcs *f, void *(*get)( const char *name ) )
{
#define GLPFX_GET(name) (*(void **)&f->name = get( "gl" #name ))
    memset( f, 0, sizeof(*f) );
    GLPFX_GET(GetIntegerv); GLPFX_GET(GetBooleanv); GLPFX_GET(GetError); GLPFX_GET(IsEnabled);
    GLPFX_GET(Enable); GLPFX_GET(Disable); GLPFX_GET(Viewport); GLPFX_GET(ColorMask); GLPFX_GET(ReadBuffer);
    GLPFX_GET(BindTexture); GLPFX_GET(GenTextures); GLPFX_GET(IsTexture); GLPFX_GET(TexImage2D);
    GLPFX_GET(TexParameteri); GLPFX_GET(DrawArrays); GLPFX_GET(BindBuffer); GLPFX_GET(ActiveTexture);
    GLPFX_GET(UseProgram); GLPFX_GET(CreateShader); GLPFX_GET(ShaderSource); GLPFX_GET(CompileShader);
    GLPFX_GET(GetShaderiv); GLPFX_GET(GetShaderInfoLog); GLPFX_GET(DeleteShader); GLPFX_GET(CreateProgram);
    GLPFX_GET(AttachShader); GLPFX_GET(LinkProgram); GLPFX_GET(GetProgramiv); GLPFX_GET(DeleteProgram);
    GLPFX_GET(IsProgram); GLPFX_GET(GetUniformLocation); GLPFX_GET(Uniform1i); GLPFX_GET(Uniform1f);
    GLPFX_GET(Uniform2f); GLPFX_GET(GenVertexArrays); GLPFX_GET(BindVertexArray); GLPFX_GET(IsVertexArray);
    GLPFX_GET(GenFramebuffers); GLPFX_GET(BindFramebuffer); GLPFX_GET(FramebufferTexture2D);
    GLPFX_GET(IsFramebuffer); GLPFX_GET(BlitFramebuffer); GLPFX_GET(BindSampler); GLPFX_GET(ClipControl);
#undef GLPFX_GET
    /* Every one but the last two is needed. */
    for (void **entry = (void **)f; entry < (void **)&f->BindSampler; entry++)
        if (!*entry) return 0;
    return 1;
}

/* The strength of FSR's RCAS for a sharpness of 0 to 1, as the Vulkan
 * presentation counts it (vulkan.c): 1 is RCAS at its strongest, 0.2 is 1.6
 * stops below; 0 is no sharpening at all. */
static inline float gl_postfx_rcas_strength( float sharpness )
{
    return sharpness > 0 ? exp2f( -2.0f * (1.0f - sharpness) ) : 0;
}

static unsigned int gl_postfx_shader( const struct gl_postfx_funcs *f, unsigned int type, const char *source,
                                      char *error, size_t error_size )
{
    unsigned int shader = f->CreateShader( type );
    int compiled = 0, length = 0;

    if (!shader) return 0;
    f->ShaderSource( shader, 1, &source, NULL );
    f->CompileShader( shader );
    f->GetShaderiv( shader, GLPFX_COMPILE_STATUS, &compiled );
    if (compiled) return shader;
    if (error_size)
    {
        int at = snprintf( error, error_size, "a shader did not compile: " );

        if (at > 0 && (size_t)at < error_size)
            f->GetShaderInfoLog( shader, (int)(error_size - (size_t)at), &length, error + at );
    }
    f->DeleteShader( shader );
    return 0;
}

static unsigned int gl_postfx_program( const struct gl_postfx_funcs *f, const char *fragment, char *error,
                                       size_t error_size )
{
    unsigned int vs = gl_postfx_shader( f, GLPFX_VERTEX_SHADER, gl_postfx_vertex, error, error_size );
    unsigned int fs = vs ? gl_postfx_shader( f, GLPFX_FRAGMENT_SHADER, fragment, error, error_size ) : 0;
    unsigned int program = fs ? f->CreateProgram() : 0;
    int linked = 0;

    if (program)
    {
        f->AttachShader( program, vs );
        f->AttachShader( program, fs );
        f->LinkProgram( program );
        f->GetProgramiv( program, GLPFX_LINK_STATUS, &linked );
        if (!linked)
        {
            snprintf( error, error_size, "a program did not link" );
            f->DeleteProgram( program );
            program = 0;
        }
    }
    if (vs) f->DeleteShader( vs );
    if (fs) f->DeleteShader( fs );
    return program;
}

/* The programs, made once in the context; the program may delete names it
 * never made, so each is checked before use and made again. */
static int gl_postfx_prepare( const struct gl_postfx_funcs *f, struct gl_postfx *fx, char *error, size_t size )
{
    if (fx->fxaa && f->IsProgram( fx->fxaa ) && fx->easu && f->IsProgram( fx->easu ) &&
        fx->rcas && f->IsProgram( fx->rcas ) && fx->array && f->IsVertexArray( fx->array ))
        return 1;
    if (!(fx->fxaa = gl_postfx_program( f, gl_postfx_fxaa, error, size )) ||
        !(fx->easu = gl_postfx_program( f, gl_postfx_easu, error, size )) ||
        !(fx->rcas = gl_postfx_program( f, gl_postfx_rcas, error, size )))
        return 0;
    fx->fxaa_rcp = f->GetUniformLocation( fx->fxaa, "rcp" );
    fx->easu_scale = f->GetUniformLocation( fx->easu, "scale" );
    fx->easu_offset = f->GetUniformLocation( fx->easu, "offset" );
    fx->rcas_sharpness = f->GetUniformLocation( fx->rcas, "sharpness" );
    f->UseProgram( fx->fxaa );
    f->Uniform1i( f->GetUniformLocation( fx->fxaa, "frame" ), 0 );
    f->UseProgram( fx->easu );
    f->Uniform1i( f->GetUniformLocation( fx->easu, "frame" ), 0 );
    f->UseProgram( fx->rcas );
    f->Uniform1i( f->GetUniformLocation( fx->rcas, "frame" ), 0 );
    f->GenVertexArrays( 1, &fx->array );
    f->BindVertexArray( fx->array );  /* a name is an array once bound */
    return 1;
}

/* A texture of width x height and a framebuffer drawing into it. */
static void gl_postfx_target( const struct gl_postfx_funcs *f, unsigned int *texture, unsigned int *framebuffer,
                              int width, int height, int resized )
{
    if (!*texture || !f->IsTexture( *texture )) f->GenTextures( 1, texture ), resized = 1;
    f->BindTexture( GLPFX_TEXTURE_2D, *texture );
    if (resized)
    {
        f->TexImage2D( GLPFX_TEXTURE_2D, 0, GLPFX_RGBA8, width, height, 0, GLPFX_RGBA, GLPFX_UNSIGNED_BYTE, NULL );
        f->TexParameteri( GLPFX_TEXTURE_2D, GLPFX_TEXTURE_MIN_FILTER, GLPFX_LINEAR );
        f->TexParameteri( GLPFX_TEXTURE_2D, GLPFX_TEXTURE_MAG_FILTER, GLPFX_LINEAR );
        f->TexParameteri( GLPFX_TEXTURE_2D, GLPFX_TEXTURE_WRAP_S, GLPFX_CLAMP_TO_EDGE );
        f->TexParameteri( GLPFX_TEXTURE_2D, GLPFX_TEXTURE_WRAP_T, GLPFX_CLAMP_TO_EDGE );
    }
    if (!*framebuffer || !f->IsFramebuffer( *framebuffer )) f->GenFramebuffers( 1, framebuffer ), resized = 1;
    f->BindFramebuffer( GLPFX_DRAW_FRAMEBUFFER, *framebuffer );
    if (resized)
        f->FramebufferTexture2D( GLPFX_DRAW_FRAMEBUFFER, GLPFX_COLOR_ATTACHMENT0, GLPFX_TEXTURE_2D, *texture, 0 );
}

struct gl_postfx_saved
{
    int read_fb, draw_fb, read_buffer, program, array, active, texture, sampler, unpack, viewport[4];
    int clip_origin, clip_depth;
    unsigned char mask[4];
    unsigned char blend, depth, stencil, cull, scissor, discard, srgb, logic;
};

static void gl_postfx_save( const struct gl_postfx_funcs *f, struct gl_postfx_saved *s )
{
    f->GetIntegerv( GLPFX_READ_FRAMEBUFFER_BINDING, &s->read_fb );
    f->GetIntegerv( GLPFX_DRAW_FRAMEBUFFER_BINDING, &s->draw_fb );
    f->GetIntegerv( GLPFX_CURRENT_PROGRAM, &s->program );
    f->GetIntegerv( GLPFX_VERTEX_ARRAY_BINDING, &s->array );
    f->GetIntegerv( GLPFX_ACTIVE_TEXTURE, &s->active );
    f->GetIntegerv( GLPFX_PIXEL_UNPACK_BUFFER_BINDING, &s->unpack );
    f->GetIntegerv( GLPFX_VIEWPORT, s->viewport );
    f->GetBooleanv( GLPFX_COLOR_WRITEMASK, s->mask );
    s->blend = f->IsEnabled( GLPFX_BLEND );
    s->depth = f->IsEnabled( GLPFX_DEPTH_TEST );
    s->stencil = f->IsEnabled( GLPFX_STENCIL_TEST );
    s->cull = f->IsEnabled( GLPFX_CULL_FACE );
    s->scissor = f->IsEnabled( GLPFX_SCISSOR_TEST );
    s->discard = f->IsEnabled( GLPFX_RASTERIZER_DISCARD );
    s->srgb = f->IsEnabled( GLPFX_FRAMEBUFFER_SRGB );
    s->logic = f->IsEnabled( GLPFX_COLOR_LOGIC_OP );
    f->ActiveTexture( GLPFX_TEXTURE0 );
    f->GetIntegerv( GLPFX_TEXTURE_BINDING_2D, &s->texture );
    s->sampler = 0;
    if (f->BindSampler) f->GetIntegerv( GLPFX_SAMPLER_BINDING, &s->sampler );
    /* The window's read buffer: the copy reads the back buffer, whatever the
     * program last read from. */
    f->BindFramebuffer( GLPFX_READ_FRAMEBUFFER, 0 );
    f->GetIntegerv( GLPFX_READ_BUFFER, &s->read_buffer );
    s->clip_origin = 0;
    if (f->ClipControl)
    {
        f->GetIntegerv( GLPFX_CLIP_ORIGIN, &s->clip_origin );
        if (!s->clip_origin) f->GetError();  /* a context without clip control: the query's error is ours */
        if (s->clip_origin == GLPFX_UPPER_LEFT) f->GetIntegerv( GLPFX_CLIP_DEPTH_MODE, &s->clip_depth );
    }
}

static void gl_postfx_set( const struct gl_postfx_funcs *f, unsigned int cap, unsigned char on )
{
    if (on) f->Enable( cap );
    else f->Disable( cap );
}

static void gl_postfx_restore( const struct gl_postfx_funcs *f, const struct gl_postfx_saved *s )
{
    if (s->clip_origin == GLPFX_UPPER_LEFT) f->ClipControl( GLPFX_UPPER_LEFT, s->clip_depth );
    f->BindFramebuffer( GLPFX_READ_FRAMEBUFFER, 0 );
    f->ReadBuffer( s->read_buffer );
    f->BindFramebuffer( GLPFX_READ_FRAMEBUFFER, s->read_fb );
    f->BindFramebuffer( GLPFX_DRAW_FRAMEBUFFER, s->draw_fb );
    if (f->BindSampler) f->BindSampler( 0, s->sampler );
    f->BindTexture( GLPFX_TEXTURE_2D, s->texture );
    f->ActiveTexture( s->active );
    f->BindBuffer( GLPFX_PIXEL_UNPACK_BUFFER, s->unpack );
    f->BindVertexArray( s->array );
    f->UseProgram( s->program );
    f->Viewport( s->viewport[0], s->viewport[1], s->viewport[2], s->viewport[3] );
    f->ColorMask( s->mask[0], s->mask[1], s->mask[2], s->mask[3] );
    gl_postfx_set( f, GLPFX_BLEND, s->blend );
    gl_postfx_set( f, GLPFX_DEPTH_TEST, s->depth );
    gl_postfx_set( f, GLPFX_STENCIL_TEST, s->stencil );
    gl_postfx_set( f, GLPFX_CULL_FACE, s->cull );
    gl_postfx_set( f, GLPFX_SCISSOR_TEST, s->scissor );
    gl_postfx_set( f, GLPFX_RASTERIZER_DISCARD, s->discard );
    gl_postfx_set( f, GLPFX_FRAMEBUFFER_SRGB, s->srgb );
    gl_postfx_set( f, GLPFX_COLOR_LOGIC_OP, s->logic );
}

/* Draws a pass from texture into framebuffer (0: the window). */
static void gl_postfx_pass( const struct gl_postfx_funcs *f, unsigned int program, unsigned int texture,
                            unsigned int framebuffer )
{
    f->BindFramebuffer( GLPFX_DRAW_FRAMEBUFFER, framebuffer );
    f->UseProgram( program );
    f->BindTexture( GLPFX_TEXTURE_2D, texture );
    f->DrawArrays( GLPFX_TRIANGLES, 0, 3 );
}

/* Draws a pass over a target of width x height. */
static void gl_postfx_pass_sized( const struct gl_postfx_funcs *f, unsigned int program, unsigned int texture,
                                  unsigned int framebuffer, int width, int height )
{
    f->Viewport( 0, 0, width, height );
    gl_postfx_pass( f, program, texture, framebuffer );
}

/* The passes over the back buffer of a surface of width x height, the
 * program's context current, its picture the bottom-left picture_width x
 * picture_height of it (the whole of it, or for EASU less): aa one of enum
 * gl_postfx_aa, upscale nonzero for EASU where the picture is smaller than
 * the surface, rcas_strength FSR's (gl_postfx_rcas_strength), 0 for none. 1
 * when the frame was processed; 0 when there was nothing to do or the passes
 * could not be built in this context, which error then says the first time. */
static int gl_postfx_apply_scaled( const struct gl_postfx_funcs *f, struct gl_postfx *fx, int width, int height,
                                   int picture_width, int picture_height, int aa, int upscale,
                                   float rcas_strength, char *error, size_t error_size )
{
    struct gl_postfx_saved saved;
    int fxaa = aa == GL_POSTFX_AA_FXAA, rcas = rcas_strength > 0, easu, resized;
    unsigned int picture;

    if (error_size) error[0] = 0;
    if (picture_width <= 0 || picture_height <= 0 || picture_width > width || picture_height > height)
        picture_width = width, picture_height = height;
    /* EASU only enlarges: a picture of the surface's size keeps it. */
    easu = upscale && (picture_width < width || picture_height < height);
    if (!easu) picture_width = width, picture_height = height;
    if ((!fxaa && !rcas && !easu) || fx->failed || width <= 0 || height <= 0) return 0;
    gl_postfx_save( f, &saved );
    if (!gl_postfx_prepare( f, fx, error, error_size ))
    {
        fx->failed = 1;
        gl_postfx_restore( f, &saved );
        return 0;
    }
    resized = fx->width != picture_width || fx->height != picture_height;
    fx->width = picture_width;
    fx->height = picture_height;

    f->BindBuffer( GLPFX_PIXEL_UNPACK_BUFFER, 0 );
    f->ActiveTexture( GLPFX_TEXTURE0 );
    if (f->BindSampler) f->BindSampler( 0, 0 );
    gl_postfx_target( f, &fx->scene, &fx->scene_fb, picture_width, picture_height, resized );
    if (fxaa && (rcas || easu))
    {
        gl_postfx_target( f, &fx->mid, &fx->mid_fb, picture_width, picture_height,
                          fx->mid_width != picture_width || fx->mid_height != picture_height );
        fx->mid_width = picture_width;
        fx->mid_height = picture_height;
    }
    if (easu && rcas)
    {
        gl_postfx_target( f, &fx->up, &fx->up_fb, width, height, fx->up_width != width || fx->up_height != height );
        fx->up_width = width;
        fx->up_height = height;
    }
    f->BindVertexArray( fx->array );
    if (saved.clip_origin == GLPFX_UPPER_LEFT) f->ClipControl( GLPFX_LOWER_LEFT, saved.clip_depth );
    f->Disable( GLPFX_BLEND );
    f->Disable( GLPFX_DEPTH_TEST );
    f->Disable( GLPFX_STENCIL_TEST );
    f->Disable( GLPFX_CULL_FACE );
    f->Disable( GLPFX_SCISSOR_TEST );
    f->Disable( GLPFX_RASTERIZER_DISCARD );
    f->Disable( GLPFX_FRAMEBUFFER_SRGB );
    f->Disable( GLPFX_COLOR_LOGIC_OP );
    f->ColorMask( 1, 1, 1, 1 );

    /* The picture, as it is in the back buffer. */
    f->BindFramebuffer( GLPFX_READ_FRAMEBUFFER, 0 );
    f->ReadBuffer( GLPFX_BACK );
    f->BindFramebuffer( GLPFX_DRAW_FRAMEBUFFER, fx->scene_fb );
    f->BlitFramebuffer( 0, 0, picture_width, picture_height, 0, 0, picture_width, picture_height,
                        GLPFX_COLOR_BUFFER_BIT, GLPFX_NEAREST );
    picture = fx->scene;

    if (fxaa)
    {
        f->UseProgram( fx->fxaa );
        f->Uniform2f( fx->fxaa_rcp, 1.0f / picture_width, 1.0f / picture_height );
        gl_postfx_pass_sized( f, fx->fxaa, picture, rcas || easu ? fx->mid_fb : 0, picture_width, picture_height );
        picture = fx->mid;
    }
    if (easu)
    {
        float scale_x = (float)picture_width / width, scale_y = (float)picture_height / height;

        f->UseProgram( fx->easu );
        f->Uniform2f( fx->easu_scale, scale_x, scale_y );
        f->Uniform2f( fx->easu_offset, 0.5f * scale_x - 0.5f, 0.5f * scale_y - 0.5f );
        gl_postfx_pass_sized( f, fx->easu, picture, rcas ? fx->up_fb : 0, width, height );
        picture = fx->up;
    }
    if (rcas)
    {
        f->UseProgram( fx->rcas );
        f->Uniform1f( fx->rcas_sharpness, rcas_strength );
        gl_postfx_pass_sized( f, fx->rcas, picture, 0, width, height );
    }
    gl_postfx_restore( f, &saved );
    return 1;
}

/* The passes over the whole back buffer of a surface of width x height. */
static inline int gl_postfx_apply( const struct gl_postfx_funcs *f, struct gl_postfx *fx, int width, int height,
                                   int aa, float rcas_strength, char *error, size_t error_size )
{
    return gl_postfx_apply_scaled( f, fx, width, height, width, height, aa, 0, rcas_strength, error, error_size );
}

#endif
