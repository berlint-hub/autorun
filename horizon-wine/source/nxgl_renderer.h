/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 *
 * Which OpenGL renderer a driver answered with, read from the string it gives
 * for GL_RENDERER, against the one the runtime asked for.
 *
 * The runtime asks mesa-switch for a renderer per program (MESA_SWITCH_GL_DRIVER
 * in horizon-wine/source/runtime.c: "zink" or "nvc0"), but asking is not getting:
 * mesa-switch reads that variable in switch_initialize(), when an EGL display is
 * initialized, and a display picks one backend for its lifetime. A process can
 * therefore ask for Zink and be answered by NVC0 -- the compositor's display was
 * already up when the program's first context was made, or the setting reached
 * the runtime after something else had made one -- and nothing in the runtime's
 * log said so, because it only knew what it had asked for. (A display that is
 * terminated and made again does read the variable afresh: switch_terminate()
 * frees the backend state, so the launcher's own SDL display does not stand in
 * a program's way.)
 *
 * The program's own GL_RENDERER is the one place that answers the question:
 * Zink names itself first ("zink (Mesa)", "zink Vulkan 1.4 (NVIDIA Tegra X1
 * (NVK ...))"), and nvc0 names the card ("NV132", "NVIDIA Tegra X1"). Used by
 * dlls/win32u/winnx_opengl.c where the first context is made current, and by
 * tests/nxgl_renderer.c without a console.
 */
#ifndef WINE_NXGL_RENDERER_H
#define WINE_NXGL_RENDERER_H

#include <ctype.h>

/* What the runtime asked for. */
enum nxgl_renderer
{
    NXGL_RENDERER_NVC0 = 0,   /* Mesa's native nouveau driver */
    NXGL_RENDERER_ZINK = 1,   /* Zink over NVK/Vulkan */
};

/* The renderer "MESA_SWITCH_GL_DRIVER" names, as mesa-switch reads it: "zink"
 * is Zink, and "nvc0", "nouveau", nothing at all and anything unrecognised are
 * all the display's own default. */
static inline enum nxgl_renderer nxgl_renderer_requested( const char *env )
{
    static const char zink[] = "zink";
    unsigned int i;

    if (!env) return NXGL_RENDERER_NVC0;
    for (i = 0; i < sizeof(zink) - 1; i++)
        if (tolower( (unsigned char)env[i] ) != zink[i]) return NXGL_RENDERER_NVC0;
    return env[i] == '\0' ? NXGL_RENDERER_ZINK : NXGL_RENDERER_NVC0;
}

/* A renderer string that is Zink's, whatever follows the name. */
static inline int nxgl_renderer_is_zink( const char *renderer )
{
    static const char zink[] = "zink";
    unsigned int i;

    if (!renderer) return 0;
    for (i = 0; i < sizeof(zink) - 1; i++)
        if (tolower( (unsigned char)renderer[i] ) != zink[i]) return 0;
    /* "zink", "zink (Mesa)", "zink Vulkan 1.4 (..." -- but not a longer word
     * that merely starts the same way. */
    return renderer[i] == '\0' || renderer[i] == ' ' || renderer[i] == '(';
}

/* How the answer stands against the request. */
enum nxgl_renderer_state
{
    NXGL_RENDERER_AS_ASKED = 0,   /* the renderer that answered is the one asked for */
    NXGL_RENDERER_OTHER,          /* the display had another backend, or none was asked for */
    NXGL_RENDERER_NO_ANSWER,      /* the driver gave no renderer string at all */
};

static inline enum nxgl_renderer_state nxgl_renderer_state_of( enum nxgl_renderer requested,
                                                              const char *renderer )
{
    if (!renderer || !renderer[0]) return NXGL_RENDERER_NO_ANSWER;
    if (nxgl_renderer_is_zink( renderer ) == (requested == NXGL_RENDERER_ZINK))
        return NXGL_RENDERER_AS_ASKED;
    return NXGL_RENDERER_OTHER;
}

#endif /* WINE_NXGL_RENDERER_H */
