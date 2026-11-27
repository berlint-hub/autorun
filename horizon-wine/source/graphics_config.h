#ifndef WINE_NX_GRAPHICS_CONFIG_H
#define WINE_NX_GRAPHICS_CONFIG_H

void wine_nx_graphics_configure(int frame_limit, int vsync);
void wine_nx_upscaling_configure(int mode, float sharpness);
/* An OpenGL program's frame at its swap (winnx_opengl.c, gl_postfx.h): the
 * anti-aliasing, and with the Upscaling setting on FSR 1.0, FSR's sharpening
 * of a picture drawn at the surface's size. */
void wine_nx_gl_postfx_configure(int anti_aliasing, int upscaling, float sharpness);

#endif
