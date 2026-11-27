#!/usr/bin/env python3
"""Post-processing of an OpenGL program's frame at its swap (source/gl_postfx.h).

The passes themselves run over Mesa in tests/gl_postfx.c; this holds how they
are reached: the program's settings to the runtime, the runtime to win32u,
and win32u drawing them at the swap, over the game's picture and under the
overlays.
"""
from pathlib import Path
import re
import sys

root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
wine = root / 'horizon-wine'

postfx = (wine / 'source/gl_postfx.h').read_text()
assert 'enum gl_postfx_aa { GL_POSTFX_AA_OFF, GL_POSTFX_AA_FXAA, GL_POSTFX_AA_COUNT };' in postfx
# RCAS counts sharpness as the Vulkan presentation does.
assert 'exp2f( -2.0f * (1.0f - sharpness) )' in postfx
vulkan = (root / 'dlls/win32u/vulkan.c').read_text()
assert 'exp2f( -2.0f * (1.0f - nx_upscaling_sharpness) )' in vulkan
# Everything set is put back: each enable it turns off is turned back on.
for cap in ('BLEND', 'DEPTH_TEST', 'STENCIL_TEST', 'CULL_FACE', 'SCISSOR_TEST', 'RASTERIZER_DISCARD',
            'FRAMEBUFFER_SRGB', 'COLOR_LOGIC_OP'):
    assert f'f->Disable( GLPFX_{cap} );' in postfx and f'gl_postfx_set( f, GLPFX_{cap}, s->' in postfx, cap

winnx = (root / 'dlls/win32u/winnx_opengl.c').read_text()
assert '#include "../../horizon-wine/source/gl_postfx.h"' in winnx
configure = winnx[winnx.index('void wine_nx_gl_postfx_configure( int anti_aliasing, int upscaling, float sharpness )'):]
configure = configure[:configure.index('\n}\n')]
assert 'nx_postfx_rcas = upscaling == 1 ? gl_postfx_rcas_strength( sharpness ) : 0;' in configure
assert 'nx_postfx_upscale = upscaling == 1;' in configure
draw = winnx[winnx.index('static void nx_osk_draw_gl( const struct nx_osk_target *target )\n{'):]
draw = draw[:draw.index('\n}\n')]
# Under the lock, in the program's context, before any overlay is drawn.
at = draw.index('if (postfx_on) nx_postfx_draw( osk, width, height, target->picture );')
assert draw.index('pthread_mutex_lock( &nx_osk_gl_lock );') < at < draw.index('    if (overlay_visible)\n')
assert at < draw.index('nx_fps_gl_draw( osk, height );')

# FSR's upscaling: with it, a mode below the screen's keeps a surface of the
# screen's size, its picture in the corner, and EASU enlarges that at the swap
# with the size the program's thread saw; without it, or in a mode of the
# screen's size, the surface is the picture as before.
fit = winnx[winnx.index('static void nx_screen_fit( struct opengl_drawable *base )\n{'):]
fit = fit[:fit.index('\n}\n')]
assert fit.index('nx_easu_picture = picture;') < fit.index('if (emulate_modeset) return;')
assert 'if (nx_postfx_upscale && want.cx <= screen.cx && want.cy <= screen.cy &&' in fit
assert fit.index('picture = want;') < fit.index('want = screen;') < fit.index('resize( egl->display, base->surface, want.cx, want.cy )')
assert fit.count('nx_easu_picture.cx = nx_easu_picture.cy = 0;') == 2  # no resize, or a refused one
setup = winnx[winnx.index('static void nx_osk_draw( struct opengl_drawable *base )\n{'):]
setup = setup[:setup.index('\n}\n')]
assert setup.index('target.picture = nx_easu_picture;') < setup.index('horizon_egl_next_swap_hook( nx_osk_draw_hook, slot );')
assert '(nx_postfx_upscale && target->picture.cx)' in draw
postfx_draw = winnx[winnx.index('static void nx_postfx_draw('):]
postfx_draw = postfx_draw[:postfx_draw.index('\n}\n')]
assert 'gl_postfx_apply_scaled( &nx_postfx_funcs, &osk->postfx, width, height, picture.cx, picture.cy,' in postfx_draw

runtime = (wine / 'source/runtime.c').read_text()
assert 'wine_nx_gl_postfx_configure( 0, 0, 0.4f );' in runtime
assert re.search(r'wine_nx_gl_postfx_configure\( settings\.anti_aliasing, settings\.upscaling,\s*'
                 r'launcher_sharpness_values\[settings\.upscaling_sharpness\] \);', runtime)
assert 'void wine_nx_gl_postfx_configure(int anti_aliasing, int upscaling, float sharpness);' in \
    (wine / 'source/graphics_config.h').read_text()

launcher = (wine / 'source/launcher.c').read_text()
assert 'ADD_ROW( ROW_ANTI_ALIASING, SECTION_GRAPHICS, "Anti-aliasing",' in launcher
assert 'case ROW_ANTI_ALIASING:' in launcher
settings = (wine / 'source/launcher_settings.h').read_text()
assert 'static const char *const launcher_anti_aliasing_values[] = { "off", "fxaa" };' in settings
print('OpenGL post-processing: settings, the runtime, the swap under the overlays, FSR upscaling of a smaller '
      'mode and the state put back')
