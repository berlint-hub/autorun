/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 *
 * Which OpenGL renderer a driver answered with (GL_RENDERER), against the one
 * the per-game setting asked mesa-switch for.
 *
 * The asking and the answering are different things: mesa-switch reads
 * MESA_SWITCH_GL_DRIVER when an EGL display is initialized, and a display keeps
 * the backend it was made with -- a display that is terminated and made again
 * reads the variable afresh (switch_terminate frees the backend state), one that
 * is still up does not. A program whose setting says Zink can therefore run on
 * NVC0 with every other line of the log looking right, which is what "Zink never
 * turns on" looked like from outside: the runtime's line named what it asked
 * for, and nothing named what answered.
 *
 * Zink names itself first in GL_RENDERER ("zink", "zink (Mesa)", "zink Vulkan
 * 1.4 (NVIDIA Tegra X1 (NVK ...))"); native NVC0 names the card ("NV132",
 * "NVIDIA Tegra X1"). Used by dlls/win32u/winnx_opengl.c where the program's own
 * context is first made current, and by tests/nxgl_renderer.c without a console.
 */
#ifndef WINE_NXGL_RENDERER_H
#define WINE_NXGL_RENDERER_H

#include <ctype.h>

/* What the runtime asked mesa-switch for. */
enum nxgl_renderer
{
    NXGL_RENDERER_NVC0 = 0,   /* Mesa's native nouveau driver */
    NXGL_RENDERER_ZINK = 1,   /* Zink over NVK/Vulkan */
};

/* Whether a GL_RENDERER string is Zink's: the name, then the end, a space or a
 * parenthesis -- so "zinking" and "azink" are not Zink, and the drivers' own
 * decorations ("(Mesa)", "Vulkan 1.4 (...)") do not matter. */
static inline int nxgl_renderer_is_zink( const char *renderer )
{
    static const char name[] = "zink";
    unsigned int i;

    if (!renderer) return 0;
    for (i = 0; i < sizeof(name) - 1; i++)
    {
        if (!renderer[i] || tolower( (unsigned char)renderer[i] ) != name[i]) return 0;
    }
    return renderer[i] == 0 || renderer[i] == ' ' || renderer[i] == '(';
}

/* Whether the answer is the one that was asked for. A driver that gives no
 * renderer string at all contradicts nothing and is not called a mismatch. */
static inline int nxgl_renderer_matches( int asked_zink, const char *renderer )
{
    if (!renderer || !renderer[0]) return 1;
    return nxgl_renderer_is_zink( renderer ) == !!asked_zink;
}

/* The renderer "MESA_SWITCH_GL_DRIVER" names, as mesa-switch reads it: "zink" is
 * Zink; "nvc0", "nouveau", nothing at all and anything unrecognised are the
 * display's own default. */
static inline enum nxgl_renderer nxgl_renderer_requested( const char *env )
{
    static const char zink[] = "zink";
    unsigned int i;

    if (!env) return NXGL_RENDERER_NVC0;
    for (i = 0; i < sizeof(zink) - 1; i++)
        if (tolower( (unsigned char)env[i] ) != zink[i]) return NXGL_RENDERER_NVC0;
    return env[i] == 0 ? NXGL_RENDERER_ZINK : NXGL_RENDERER_NVC0;
}

/* How the answer stands against the request, for the log line under the first
 * context made current. */
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
    if (nxgl_renderer_matches( requested == NXGL_RENDERER_ZINK, renderer )) return NXGL_RENDERER_AS_ASKED;
    return NXGL_RENDERER_OTHER;
}

#endif /* WINE_NXGL_RENDERER_H */
