/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 * The GL calls the compositor's drawing makes, counted against a table of
 * stubs: state that never changes is set once, an upload touches the pixel
 * store only when its rows are not contiguous, and a frame does not re-set
 * what init put there. Run on any machine, with no GL:
 *   cc -std=gnu11 -Wall -Wextra -Werror -I tests/gl-shims -I source \
 *      -o compositor-gl-calls tests/compositor_gl_calls.c source/compositor_gl.c
 *   ./compositor-gl-calls */
#include <stdio.h>
#include <string.h>

#include "compositor_gl.h"

static int failures;

#define check( cond, what )                                                              \
    do                                                                                   \
    {                                                                                    \
        if (!(cond))                                                                     \
        {                                                                                \
            fprintf( stderr, "FAIL: %s\n", what );                                       \
            failures++;                                                                  \
        }                                                                                \
    } while (0)

/* What the compositor asked the driver for since the last reset. */
static struct
{
    int alignment, row_length, clear_color, clear, viewport, active_texture, uniform1i;
    int tex_sub_image, draw, tex_image;
    int alignment_value, row_length_value;
    int tex_x, tex_y, tex_width, tex_height, tex_stride;
    const uint32_t *tex_pixels;
} counts;

static void reset_counts( void )
{
    memset( &counts, 0, sizeof(counts) );
    counts.row_length_value = -1;
}

static GLuint next_name = 1;

static void stub_ActiveTexture( GLenum unit ) { (void)unit; counts.active_texture++; }
static void stub_AttachShader( GLuint program, GLuint shader ) { (void)program; (void)shader; }
static void stub_BindAttribLocation( GLuint program, GLuint index, const GLchar *name )
{ (void)program; (void)index; (void)name; }
static void stub_BindBuffer( GLenum target, GLuint buffer ) { (void)target; (void)buffer; }
static void stub_BindTexture( GLenum target, GLuint texture ) { (void)target; (void)texture; }
static void stub_BindVertexArray( GLuint array ) { (void)array; }
static void stub_BlendFunc( GLenum src, GLenum dst ) { (void)src; (void)dst; }
static void stub_BufferData( GLenum target, GLsizeiptr size, const void *data, GLenum usage )
{ (void)target; (void)size; (void)data; (void)usage; }
static void stub_Clear( GLbitfield mask ) { (void)mask; counts.clear++; }
static void stub_ClearColor( GLfloat r, GLfloat g, GLfloat b, GLfloat a )
{ (void)r; (void)g; (void)b; (void)a; counts.clear_color++; }
static void stub_CompileShader( GLuint shader ) { (void)shader; }
static GLuint stub_CreateProgram( void ) { return next_name++; }
static GLuint stub_CreateShader( GLenum type ) { (void)type; return next_name++; }
static void stub_DeleteBuffers( GLsizei n, const GLuint *buffers ) { (void)n; (void)buffers; }
static void stub_DeleteProgram( GLuint program ) { (void)program; }
static void stub_DeleteShader( GLuint shader ) { (void)shader; }
static void stub_DeleteTextures( GLsizei n, const GLuint *textures ) { (void)n; (void)textures; }
static void stub_DeleteVertexArrays( GLsizei n, const GLuint *arrays ) { (void)n; (void)arrays; }
static void stub_Disable( GLenum cap ) { (void)cap; }
static void stub_DrawArrays( GLenum mode, GLint first, GLsizei count )
{ (void)mode; (void)first; (void)count; counts.draw++; }
static void stub_Enable( GLenum cap ) { (void)cap; }
static void stub_EnableVertexAttribArray( GLuint index ) { (void)index; }
static void stub_GenBuffers( GLsizei n, GLuint *buffers )
{ for (int i = 0; i < n; i++) buffers[i] = next_name++; }
static void stub_GenTextures( GLsizei n, GLuint *textures )
{ for (int i = 0; i < n; i++) textures[i] = next_name++; }
static void stub_GenVertexArrays( GLsizei n, GLuint *arrays )
{ for (int i = 0; i < n; i++) arrays[i] = next_name++; }
static GLenum stub_GetError( void ) { return GL_NO_ERROR; }
static void stub_GetProgramInfoLog( GLuint program, GLsizei size, GLsizei *length, GLchar *log )
{ (void)program; if (size > 0) log[0] = 0; if (length) *length = 0; }
static void stub_GetProgramiv( GLuint program, GLenum name, GLint *value )
{ (void)program; (void)name; *value = 1; }
static void stub_GetShaderInfoLog( GLuint shader, GLsizei size, GLsizei *length, GLchar *log )
{ (void)shader; if (size > 0) log[0] = 0; if (length) *length = 0; }
static void stub_GetShaderiv( GLuint shader, GLenum name, GLint *value )
{ (void)shader; (void)name; *value = 1; }
static const GLubyte *stub_GetString( GLenum name ) { (void)name; return (const GLubyte *)"stub"; }
/* The sampler's location, so the check counts the call that points the sampler
 * at the unit -- the one init makes -- and not the per-quad "which texture is
 * this" uniform, which a frame does set. */
static GLint image_location;
static GLint stub_GetUniformLocation( GLuint program, const GLchar *name )
{
    (void)program;
    if (!strcmp( name, "image" )) return image_location = 1;
    if (!strcmp( name, "rect" )) return 2;
    if (!strcmp( name, "src" )) return 3;
    return 4;
}
static void stub_LinkProgram( GLuint program ) { (void)program; }
static void stub_PixelStorei( GLenum name, GLint value )
{
    if (name == GL_UNPACK_ALIGNMENT) { counts.alignment++; counts.alignment_value = value; }
    else if (name == GL_UNPACK_ROW_LENGTH) { counts.row_length++; counts.row_length_value = value; }
}
static void stub_ShaderSource( GLuint shader, GLsizei count, const GLchar *const *source, const GLint *length )
{ (void)shader; (void)count; (void)source; (void)length; }
static void stub_TexImage2D( GLenum target, GLint level, GLint internal, GLsizei width, GLsizei height,
                             GLint border, GLenum format, GLenum type, const void *pixels )
{ (void)target; (void)level; (void)internal; (void)width; (void)height; (void)border; (void)format;
  (void)type; (void)pixels; counts.tex_image++; }
static void stub_TexParameteri( GLenum target, GLenum name, GLint value )
{ (void)target; (void)name; (void)value; }
static void stub_TexSubImage2D( GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height,
                                GLenum format, GLenum type, const void *pixels )
{
    (void)target; (void)level; (void)format; (void)type;
    counts.tex_sub_image++;
    counts.tex_x = x; counts.tex_y = y; counts.tex_width = width; counts.tex_height = height;
    counts.tex_stride = counts.row_length_value > 0 ? counts.row_length_value : width;
    counts.tex_pixels = pixels;
}
static void stub_Uniform1i( GLint location, GLint value )
{
    (void)value;
    if (location == image_location) counts.uniform1i++;
}
static void stub_Uniform4f( GLint location, GLfloat a, GLfloat b, GLfloat c, GLfloat d )
{ (void)location; (void)a; (void)b; (void)c; (void)d; }
static void stub_UseProgram( GLuint program ) { (void)program; }
static void stub_VertexAttribPointer( GLuint index, GLint size, GLenum type, GLboolean normalized,
                                      GLsizei stride, const void *pointer )
{ (void)index; (void)size; (void)type; (void)normalized; (void)stride; (void)pointer; }
static void stub_Viewport( GLint x, GLint y, GLsizei width, GLsizei height )
{ (void)x; (void)y; (void)width; (void)height; counts.viewport++; }

static const struct compositor_gl_funcs funcs =
{
#define USE_GL_FUNC( ret, name, args ) .name = stub_##name,
    COMPOSITOR_GL_FUNCS
#undef USE_GL_FUNC
};

int main( void )
{
    struct compositor_gl comp;
    struct compositor_gl_texture window = {0}, other = {0};
    struct compositor_gl_quad quads[2];
    uint32_t pixels[16 * 8];
    int i;

    for (i = 0; i < 16 * 8; i++) pixels[i] = (uint32_t)i;

    reset_counts();
    if (compositor_gl_init( &comp, &funcs, 1280, 720 ))
    {
        fprintf( stderr, "init failed: %s\n", comp.error );
        return 1;
    }

    /* What init puts in place: the byte alignment uploads use, packed rows, the
     * clear colour, and the one texture unit the sampler reads. Setting these
     * per upload or per frame is driver work for the same state every time. */
    check( counts.alignment == 1 && counts.alignment_value == 4, "init sets the 4-byte alignment once" );
    check( counts.row_length == 1 && counts.row_length_value == 0, "init leaves the rows packed, once" );
    check( counts.clear_color == 1, "init sets the clear colour" );
    check( counts.active_texture == 1, "init chooses the texture unit" );
    check( counts.uniform1i == 1, "init points the sampler at it" );

    /* A whole surface, rows packed: nothing about the pixel store changes. */
    reset_counts();
    compositor_gl_upload( &comp, &window, 16, 8, pixels, 16, 0, 0, 16, 8 );
    check( counts.tex_sub_image == 1, "a window goes up in one upload" );
    check( counts.row_length == 0 && counts.alignment == 0, "a packed upload touches no pixel store state" );

    /* Columns of a wider surface: the rows are a stride apart, and the upload
     * has to say so. */
    reset_counts();
    compositor_gl_upload( &comp, &window, 16, 8, pixels, 16, 4, 2, 12, 6 );
    check( counts.row_length == 1 && counts.row_length_value == 16, "a partial column range sets the row length" );
    check( counts.tex_sub_image == 1 && counts.tex_x == 4 && counts.tex_y == 2 &&
           counts.tex_width == 8 && counts.tex_height == 4, "and uploads that rectangle" );
    check( counts.tex_pixels == pixels + 2 * 16 + 4, "from the pixel the rectangle starts at" );

    /* Back to a whole surface: the stride is no longer the row length. */
    reset_counts();
    compositor_gl_upload( &comp, &window, 16, 8, pixels, 16, 0, 0, 16, 8 );
    check( counts.row_length == 1 && counts.row_length_value == 0, "packed rows are put back for the next upload" );

    compositor_gl_upload( &comp, &other, 16, 8, pixels, 16, 0, 0, 16, 8 );

    quads[0].texture = &window;
    quads[0].x = 0; quads[0].y = 0; quads[0].width = 1280; quads[0].height = 720;
    quads[0].src_x = quads[0].src_y = 0;
    quads[1].texture = &other;
    quads[1].x = 100; quads[1].y = 100; quads[1].width = 320; quads[1].height = 200;
    quads[1].src_x = quads[1].src_y = 0;

    /* A frame: the screen is cleared, the two windows drawn and the pointer on
     * top, and no state init already set is set again. */
    reset_counts();
    compositor_gl_draw( &comp, quads, 2, 300, 400, 1 );
    check( counts.clear == 1 && counts.viewport == 1, "a frame clears the screen and sets the viewport once" );
    check( counts.clear_color == 0, "a frame does not set the clear colour again" );
    check( counts.active_texture == 0 && counts.uniform1i == 0, "a frame does not re-select the texture unit" );
    check( counts.tex_sub_image == 0 && counts.row_length == 0, "a frame uploads nothing" );
    check( counts.draw == 3, "two windows and the pointer are three draws" );

    /* Nothing else in the frame's path puts state back. */
    reset_counts();
    compositor_gl_destroy( &comp );
    check( counts.clear_color == 0 && counts.active_texture == 0 && counts.row_length == 0,
           "releasing the compositor sets no state either" );

    if (failures)
    {
        fprintf( stderr, "%d check(s) failed\n", failures );
        return 1;
    }
    printf( "Compositor GL calls: init sets the constant state once, an upload touches the row length only when "
            "its rows are not contiguous, and a frame draws without setting it again passed\n" );
    return 0;
}
