#!/usr/bin/env python3
"""Run the NX driver's actual popup blits against a host framebuffer."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'dlls/win32u/winnx_drv.c').read_text()
if '--baseline' in sys.argv:
    source = subprocess.check_output(['git', '-C', str(root), 'show', 'HEAD:dlls/win32u/winnx_drv.c'], text=True)
core = source[source.index('struct wine_nx_surface\n'):source.index('/**********************************************************************')]
position = source[source.index('void wine_nx_drv_WindowPosChanged('):]
fixture = r'''
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int BOOL, LONG;
typedef unsigned int UINT, DWORD;
typedef uintptr_t HWND, ULONG_PTR;
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { LONG x, y; } POINT;
typedef struct { struct { int biWidth, biHeight; } bmiHeader; DWORD bmiColors[256]; } BITMAPINFO;
#define TRUE 1
#define FALSE 0
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define max(a,b) ((a) > (b) ? (a) : (b))
#define min(a,b) ((a) < (b) ? (a) : (b))
#define FIELD_OFFSET(t,f) offsetof(t,f)
#define CONTAINING_RECORD(p,t,f) ((t *)((char *)(p) - offsetof(t,f)))
#define WS_POPUP 0x80000000u
#define GWL_STYLE -16
#define GW_OWNER 4
#define GW_CHILD 5
#define GW_HWNDNEXT 2
#define GA_ROOT 2
#define SWP_HIDEWINDOW 0x80
#define RDW_INVALIDATE 1
#define RDW_ERASE 4
#define RDW_FRAME 0x400
#define RDW_ALLCHILDREN 0x80
#define RDW_UPDATENOW 0x100
struct window_rects { RECT window, client, visible; };
struct window_surface;
/* A DPI as Wine's own server protocol holds it (include/wine/server_protocol.h). */
struct ratio { unsigned short num, den; };
struct window_surface_funcs {
    const UINT size;
    void (*set_clip)(struct window_surface *, const RECT *, UINT);
    BOOL (*flush)(struct window_surface *, const RECT *, const RECT *, const BITMAPINFO *, const void *, BOOL, const BITMAPINFO *, const void *);
    void (*destroy)(struct window_surface *);
};
struct window_surface {
    const struct window_surface_funcs *funcs;
    HWND hwnd;
    RECT rect, bounds;
    DWORD *pixels;
};
static DWORD framebuffer[320*300];
static int presents;
static struct window_surface *surfaces[4];
static void SetRectEmpty(RECT *r) { memset(r, 0, sizeof(*r)); }
static int IsRectEmpty(const RECT *r) { return r->left >= r->right || r->top >= r->bottom; }
static void OffsetRect(RECT *r, int x, int y) { r->left += x; r->right += x; r->top += y; r->bottom += y; }
static int intersect_rect(RECT *out, const RECT *a, const RECT *b) {
    RECT r = {a->left > b->left ? a->left : b->left, a->top > b->top ? a->top : b->top,
              a->right < b->right ? a->right : b->right, a->bottom < b->bottom ? a->bottom : b->bottom};
    *out = r; return !IsRectEmpty(out);
}
static void union_rect(RECT *out, const RECT *a, const RECT *b) {
    RECT r = {a->left < b->left ? a->left : b->left, a->top < b->top ? a->top : b->top,
              a->right > b->right ? a->right : b->right, a->bottom > b->bottom ? a->bottom : b->bottom}; *out = r;
}
static void wine_nx_runtime_trace(const char *s) {}
static int wine_nx_runtime_verbose;
static void *wine_nx_fb_lock(int *w, int *h, int *stride) { *w = *stride = 320; *h = 300; return framebuffer; }
static void wine_nx_fb_unlock(void) {}
static void wine_nx_fb_present(void) { presents++; }
static void window_surface_lock(struct window_surface *s) {}
static void window_surface_unlock(struct window_surface *s) {}
static void window_surface_release(struct window_surface *s) {}
static void window_surface_flush(struct window_surface *s) { assert(!"unexpected fallback"); }
static void *window_surface_get_color(struct window_surface *s, BITMAPINFO *info) {
    info->bmiHeader.biWidth = s->rect.right; info->bmiHeader.biHeight = -s->rect.bottom; return s->pixels;
}
static struct window_surface *window_surface_get(HWND h) { return h < 4 ? surfaces[h] : NULL; }
static struct ratio get_win_monitor_dpi(HWND h, struct ratio *raw) { (void)h; *raw = (struct ratio){96, 96}; return *raw; }
static struct window_surface *get_driver_window_surface(struct window_surface *s, struct ratio dpi) { (void)dpi; return s; }
static DWORD get_window_long(HWND h, int offset) { return h > 1 ? WS_POPUP : 0; }
/* The desktop's child chain, which is only walked to restack: every window is
 * at the top of it in turn, ending after the last one. */
static HWND get_desktop_window(void) { return (HWND)0x100; }
/* The compositor and the desktop's fit live in the runtime; the surfaces here
 * have no layers, so these only have to link. */
static int layer_places, layers_destroyed, rests;
void wine_nx_layer_place(struct wine_nx_layer *l, int x, int y, int w, int h) { assert(!l); layer_places++; }
void wine_nx_layer_update(struct wine_nx_layer *l, const uint32_t *pixels, int stride,
                          int left, int top, int right, int bottom) { assert(!l); }
void wine_nx_layer_destroy(struct wine_nx_layer *l) { assert(!l); layers_destroyed++; }
void wine_nx_compositor_restack(struct wine_nx_layer **l, int count) { rests++; }
/* The panel and the desktop are the screen, so a screen point is a panel point. */
void wine_nx_desktop_size(int *w, int *h) { *w = 320; *h = 300; }
void wine_nx_present_fit(int sw, int sh, int sx, int sy, int *dx, int *dy, int *dw, int *dh) {
    /* The panel is the whole desktop, so the fit is the whole rectangle. */
    if (dx) *dx = 0;
    if (dy) *dy = 0;
    if (dw) *dw = sw;
    if (dh) *dh = sh;
}
void wine_nx_screen_to_panel(double x, double y, int *px, int *py) { *px = x; *py = y; }
void wine_nx_panel_to_screen(int px, int py, double *x, double *y) { *x = px; *y = py; }
int wine_nx_pointer_poll(int *x, int *y, unsigned int *buttons) { *x = *y = 0; *buttons = 0; return 0; }
static HWND get_window_relative(HWND h, UINT rel) {
    return h == (HWND)0x100 ? (HWND)1 : (rel == GW_HWNDNEXT && h < 3 ? h + 1 : 0);
}
static HWND NtUserWindowFromPoint(int x, int y) { return 1; }
static HWND get_active_window(void) { return 1; }
static HWND get_focus(void) { return 1; }
static HWND NtUserGetAncestor(HWND h, UINT type) { return h; }
static void NtUserRedrawWindow(HWND h, void *r, int hrgn, UINT flags) { assert(!"unexpected redraw fallback"); }
'''
tests = r'''
static struct wine_nx_surface *create(HWND h, int w, int height, DWORD color) {
    struct wine_nx_surface *s = calloc(1, sizeof(*s));
    s->surface.funcs = &wine_nx_surface_funcs; s->surface.hwnd = h;
    s->surface.rect = (RECT){0, 0, w, height};
    s->surface.pixels = malloc(w * height * sizeof(DWORD));
    for (int i = 0; i < w * height; ++i) s->surface.pixels[i] = color;
    s->initial_redraw_done = TRUE;
    surfaces[h] = &s->surface;
    return s;
}
static void position(HWND h, int x, int y, UINT flags) {
    struct window_surface *s = surfaces[h];
    struct window_rects rects = {.visible = {x, y, x+s->rect.right, y+s->rect.bottom}};
    wine_nx_drv_WindowPosChanged(h, 0, 1, flags, &rects, s);
}
int main(void) {
    create(1, 320, 300, 0x00112233);
    create(2, 126, 220, 0x00ff0000);
    create(3, 93, 40, 0x0000ff00);
    position(1, 0, 0, 0);
    position(2, 32, 42, 0);
    assert(framebuffer[100*320+50] == 0xff0000ff);
    int before = presents;
    position(2, 32, 42, SWP_HIDEWINDOW);
    assert(framebuffer[100*320+50] == 0xff332211);
    assert(presents == before + 1);
    /* A pending paint for Edit must not bring it back after closing. */
    wine_nx_surface_present_full(surfaces[2]);
    assert(framebuffer[100*320+50] == 0xff332211);
    position(3, 62, 42, 0);
    assert(framebuffer[50*320+70] == 0xff00ff00);
    assert(framebuffer[100*320+50] == 0xff332211);
    position(3, 62, 42, SWP_HIDEWINDOW);
    wine_nx_surface_present_full(surfaces[3]);
    for (int i = 0; i < 320*300; ++i) assert(framebuffer[i] == 0xff332211);
    /* Showing the same cached surface again must re-enable its flushes. */
    position(2, 32, 42, 0);
    assert(framebuffer[100*320+50] == 0xff0000ff);
    for (int i = 1; i < 4; ++i) { free(surfaces[i]->pixels); free(surfaces[i]); }
    while (wine_nx_surface_entries) {
        struct wine_nx_surface_entry *next = wine_nx_surface_entries->next;
        free(wine_nx_surface_entries); wine_nx_surface_entries = next;
    }
    puts("PASS: menu hide restores pixels and presents; late hidden flushes suppressed; next menu and reopen");
}
'''
with tempfile.TemporaryDirectory(prefix='wine-nx-popup-') as temp:
    src = Path(temp) / 'test.c'
    exe = Path(temp) / 'test'
    src.write_text(fixture + core + position + tests)
    subprocess.run(['cc', '-g', '-Wno-pointer-bool-conversion', '-fsanitize=address,undefined',
                    '-pthread', str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
