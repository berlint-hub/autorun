/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 *
 * The controls overlay (controls_overlay.h): the controls menu over a running
 * program, worked with the controller or the touchscreen. It is the launcher's
 * editor in small, as an on-screen display: a panel down the right side of the
 * screen, a section per thing it edits -- what the controller is (Input), what
 * each of its inputs does (Mapping), and how fast the stick moves the pointer
 * (Mouse) -- and the section's rows under the tabs.
 *
 * Keyboard/mouse mode picks keys and mouse actions from a grid of the ones
 * games use; controller mode captures what is pressed. A change is written to
 * the program's controls at once (the same file the launcher edits, over the
 * game's recommended layout and the global controls), so every row can be
 * tried against the game the moment it is closed.
 *
 * The panel is placed and scaled from the screen it is drawn on -- its width,
 * its margins, its rows and its text all come from one factor of the smaller
 * of the two sides -- and a tap is mapped through the same geometry, from the
 * console's 1280x720 touch panel into whatever the presenters are drawing for.
 *
 * Its picture is drawn here, in software, with SDL_ttf over the font
 * wine_nx_osk_font gives (osk.c), and only when something on it changed.
 * osk.c hands its frame and copy calls over to this while the overlay is up,
 * so every presenter that draws the floating keyboard draws this too.
 */
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <SDL.h>
#include <SDL_ttf.h>

#include "controls_overlay.h"
#include "osk.h"

/* The runtime's copy of the profile the game runs on (runtime.c): every save
 * below hands the whole thing over at once, so a remap takes effect in the
 * game the moment it is made, not at the next launch. Weak, so the host tests
 * can link this file without the runtime. */
extern void wine_nx_input_profile_publish( const struct input_profile *profile ) __attribute__((weak));
/* And reads it: + and - may have swapped what the controller is since the card
 * was read, and the grid has to remap what the pad is doing now. */
extern void wine_nx_input_profile_copy( struct input_profile *profile ) __attribute__((weak));
/* The Input section's own switch: the same swap + and - make, for the rest of
 * the run, without the runtime guessing which of the two it was. */
extern int wine_nx_input_set_mode( int mode ) __attribute__((weak));

#define PICK_COLS 6
#define PICK_ROWS 5
#define STICK_DEAD 16000
#define REPEAT_FIRST_NS 350000000ull
#define REPEAT_NEXT_NS    70000000ull

/* The console's touch panel, in the pixels its touches come in as. */
#define TOUCH_PANEL_W 1280
#define TOUCH_PANEL_H 720

/* Generations handed out to presenters have their top bit set: the keyboard's
 * never do, so two overlays sharing one presenter cannot share a number. */
#define GENERATION_BIT 0x80000000u

/* The sections of the menu, in the order the tabs run across the panel: the
 * header's own names for them, so a test can say which one it is on. */
#define TAB_INPUT   WINE_NX_CONTROLS_TAB_INPUT
#define TAB_MAPPING WINE_NX_CONTROLS_TAB_MAPPING
#define TAB_MOUSE   WINE_NX_CONTROLS_TAB_MOUSE
#define TAB_COUNT   3
/* What the panel is showing: a section's rows, a row's grid of keys, or a
 * row's capture. */
enum { MODE_TAB, MODE_PICK, MODE_CAPTURE };

/* What the grid offers: the mouse actions, then the keys games bind. */
static const struct { unsigned short code; const char *label; } pick_targets[] =
{
    { INPUT_MOUSE_LEFT, "Left click" }, { INPUT_MOUSE_RIGHT, "Right click" },
    { INPUT_MOUSE_MIDDLE, "Middle" }, { INPUT_MOUSE_X1, "Mouse back" },
    { INPUT_MOUSE_X2, "Mouse fwd" }, { INPUT_WHEEL_UP, "Wheel up" },
    { INPUT_WHEEL_DOWN, "Wheel down" }, { INPUT_WHEEL_LEFT, "Wheel left" },
    { INPUT_WHEEL_RIGHT, "Wheel right" }, { INPUT_MOUSE_UP, "Mouse up" },
    { INPUT_MOUSE_DOWN, "Mouse down" }, { INPUT_MOUSE_MOVE_LEFT, "Mouse left" },
    { INPUT_MOUSE_MOVE_RIGHT, "Mouse right" },
    { 0x1b, "Esc" }, { 0x0d, "Enter" }, { 0x20, "Space" }, { 0x09, "Tab" },
    { 0x08, "Back" }, { 0x2e, "Del" }, { 0x10, "Shift" }, { 0x11, "Ctrl" },
    { 0x12, "Alt" }, { 0x5b, "Win" }, { 0x26, "Up" }, { 0x28, "Down" },
    { 0x25, "Left" }, { 0x27, "Right" }, { 0xc0, "`" }, { 0xbd, "-" },
    { 0xbb, "=" }, { 0xdb, "[" }, { 0xdd, "]" }, { 0xdc, "\\" },
    { 0xba, ";" }, { 0xde, "'" }, { 0xbc, "," }, { 0xbe, "." }, { 0xbf, "/" },
    { '1', "1" }, { '2', "2" }, { '3', "3" }, { '4', "4" }, { '5', "5" },
    { '6', "6" }, { '7', "7" }, { '8', "8" }, { '9', "9" }, { '0', "0" },
    { 'A', "A" }, { 'B', "B" }, { 'C', "C" }, { 'D', "D" }, { 'E', "E" },
    { 'F', "F" }, { 'G', "G" }, { 'H', "H" }, { 'I', "I" }, { 'J', "J" },
    { 'K', "K" }, { 'L', "L" }, { 'M', "M" }, { 'N', "N" }, { 'O', "O" },
    { 'P', "P" }, { 'Q', "Q" }, { 'R', "R" }, { 'S', "S" }, { 'T', "T" },
    { 'U', "U" }, { 'V', "V" }, { 'W', "W" }, { 'X', "X" }, { 'Y', "Y" },
    { 'Z', "Z" },
    { 0x70, "F1" }, { 0x71, "F2" }, { 0x72, "F3" }, { 0x73, "F4" },
    { 0x74, "F5" }, { 0x75, "F6" }, { 0x76, "F7" }, { 0x77, "F8" },
    { 0x78, "F9" }, { 0x79, "F10" }, { 0x7a, "F11" }, { 0x7b, "F12" },
};
#define PICK_COUNT ((int)(sizeof(pick_targets) / sizeof(pick_targets[0])))

static const char *const tab_labels[TAB_COUNT] = { "Input", "Mapping", "Mouse" };

/* A pressed button's row (input_sources' order), by the OSK_* bit it sets:
 * capture reads "the button you pressed" as the target it should become. */
static const unsigned char button_source[16] =
{
    12, 13, 14, 15,  /* D-pad up, down, left, right */
    0, 1, 2, 3,      /* A, B, X, Y */
    4, 5, 6, 7,      /* L, R, ZL, ZR */
    8, 9, 10, 11,    /* +, -, left stick click, right stick click */
};

static pthread_mutex_t overlay_lock = PTHREAD_MUTEX_INITIALIZER;
static int visible, mode, tab;
static struct input_profile profile;
static struct launcher_kv kv;
static char root_dir[256], program[768], save_path[768];
/* One cursor per section: the Input section has two rows, the Mouse section
 * the speed stops, the Mapping section the controller's inputs. */
static int input_selected, map_selected, map_top, mouse_selected, mouse_top;
static int pick, pick_top;
static struct input_binding draft;
static unsigned int ignored, previous, repeating;
static uint64_t repeat_at;
static int capture_last = -1;
static unsigned int generation = 1;
static int touch_down;

/* The picture last drawn, for which screen and which generation. */
static uint32_t *picture;
static int picture_screen_w, picture_screen_h, picture_w, picture_h;
static unsigned int picture_generation;
/* The screen the presenters last drew it for: touches come in on the console's
 * own panel, so this is what a finger's pixels are scaled into. */
static int touch_screen_w = TOUCH_PANEL_W, touch_screen_h = TOUCH_PANEL_H;

/* Everything the panel's placement is made of, all of it derived from the
 * screen so that no part of it is a fixed number of pixels. The strips run
 * down the panel: a title, the tabs, the section's rows, the hints. */
struct geometry
{
    int screen_w, screen_h;
    int x, y, width, height;    /* the panel, on the screen */
    int pad, gap;               /* the panel's inner margin, between rows */
    int header, tabs, footer;   /* the three strips' heights */
    int tab_w;                  /* one tab across the strip */
    int content_top;            /* from the panel's top edge */
    int content_y, content_h;   /* the rows, on the screen */
    int row_h;                  /* one row of a section */
    int rows;                   /* rows that fit */
    int text, small, tiny;      /* the three font sizes */
};

/* The one factor the whole panel is built from: its smaller side against the
 * panel's own shape, so a 1280x720 screen gives 1 and everything else scales
 * with what it actually has. */
static float layout_scale( int screen_w, int screen_h )
{
    float s = screen_h / 720.0f;

    if (screen_w * 720 < screen_h * 1280) s = screen_w / 1280.0f;
    if (s < 0.30f) s = 0.30f;
    return s;
}

static int scaled( float s, float value, int least )
{
    int result = (int)roundf( value * s );

    return result < least ? least : result;
}

static void layout( int screen_w, int screen_h, struct geometry *g )
{
    float s = layout_scale( screen_w, screen_h );
    int margin, row_space;

    g->screen_w = screen_w;
    g->screen_h = screen_h;
    margin = scaled( s, 18, 4 );
    /* A panel down the right side, a margin off every edge of the screen: the
     * game stays in view on the left, and nothing of the panel is at an edge
     * it could be cropped by. */
    g->width = scaled( s, 560, 200 );
    if (g->width > screen_w - 2 * margin) g->width = screen_w > 2 * margin ? screen_w - 2 * margin : screen_w;
    g->height = screen_h > 2 * margin ? screen_h - 2 * margin : screen_h;
    g->x = screen_w - margin - g->width;
    g->y = margin;
    g->pad = scaled( s, 14, 3 );
    g->gap = scaled( s, 6, 2 );
    g->header = scaled( s, 44, 14 );
    g->tabs = scaled( s, 38, 14 );
    g->footer = scaled( s, 36, 12 );
    g->row_h = scaled( s, 42, 12 );
    g->tab_w = (g->width - 2 * g->pad) / TAB_COUNT;
    g->content_top = g->pad + g->header + g->tabs;
    g->content_y = g->y + g->content_top;
    g->content_h = g->y + g->height - g->pad - g->footer - g->content_y;
    if (g->content_h < g->row_h) g->content_h = g->row_h;
    row_space = g->row_h + g->gap;
    g->rows = row_space > 0 ? g->content_h / row_space : 1;
    if (g->rows < 1) g->rows = 1;
    /* Text is a share of the screen, never a fixed size: the row it sits in
     * was too, so the two grow together. */
    g->text = scaled( s, 18, 7 );
    g->small = scaled( s, 15, 6 );
    g->tiny = scaled( s, 12, 5 );
}

/* The grid's cells and where the block of them starts, inside the content
 * area: cells are wider than tall, and the block is centred. */
static void grid_metrics( const struct geometry *g, int *cell_w, int *cell_h, int *offset )
{
    int area_w = g->width - 2 * g->pad;
    int rows_h;

    *cell_w = (area_w - (PICK_COLS - 1) * g->gap) / PICK_COLS;
    if (*cell_w < 8) *cell_w = 8;
    *cell_h = (int)roundf( *cell_w * 0.62f );
    if (*cell_h > (g->content_h - (PICK_ROWS - 1) * g->gap) / PICK_ROWS)
        *cell_h = (g->content_h - (PICK_ROWS - 1) * g->gap) / PICK_ROWS;
    if (*cell_h < 8) *cell_h = 8;
    rows_h = PICK_ROWS * *cell_h + (PICK_ROWS - 1) * g->gap;
    *offset = (g->content_h - rows_h) / 2;
    if (*offset < 0) *offset = 0;
}

static int controller_mapping( void )
{
    return profile.mode != INPUT_KEYBOARD_MOUSE;
}

static struct input_binding *row_binding( int source )
{
    return controller_mapping() ? profile.pad + source : profile.keys + source;
}

/* Shift, Control and Alt in their left-shoes: the grid knows one of each, and
 * a chord the launcher saved with a side's code is the same chord here. */
static unsigned short normalize_code( unsigned short code )
{
    if (code >= 0xa0 && code <= 0xa5) return (unsigned short)(0x10 + (code - 0xa0) / 2);
    return code;
}

static int has_code( const struct input_binding *binding, unsigned short code )
{
    for (unsigned int i = 0; i < binding->count; i++)
        if (normalize_code( binding->code[i] ) == normalize_code( code )) return 1;
    return 0;
}

/* Reload every layer over the file just written: the game's layout and the
 * global controls come back under the change, and a cleared row falls back to
 * whatever they say. */
static int reload( void )
{
    struct input_profile loaded;
    struct launcher_kv loaded_kv;
    char loaded_path[sizeof(save_path)];

    if (program[0] && program[1] != ':')
    {
        if (!input_profile_program( root_dir, program, profile.keyboard_auto, &loaded, &loaded_kv,
                                    loaded_path, sizeof(loaded_path) )) return 0;
    }
    else if (!input_profile_global( root_dir, profile.keyboard_auto, &loaded, &loaded_kv,
                                    loaded_path, sizeof(loaded_path) )) return 0;
    profile = loaded;
    /* What the controller is now, not what it was when the game started: a tap
     * of + and - while playing swaps it for the run (runtime.c), and this menu
     * edits the layout the player is playing on. */
    if (wine_nx_input_profile_copy)
    {
        struct input_profile live;

        wine_nx_input_profile_copy( &live );
        profile.mode = live.mode;
    }
    kv = loaded_kv;
    memcpy( save_path, loaded_path, sizeof(save_path) );
    generation++;
    if (wine_nx_input_profile_publish) wine_nx_input_profile_publish( &profile );
    return 1;
}

static void save_row( int source, const struct input_binding *binding )
{
    if (!input_binding_save( &kv, source, controller_mapping(), binding ) ||
        !input_profile_save( &kv, save_path )) return;
    reload();
}

/* The mouse speed, by its stop in input_mouse_speed_values, or -1 for the
 * game's own (the key gone, whatever is under it stands). */
static void save_speed( int index )
{
    char value[16];

    if (index < 0) launcher_kv_set( &kv, "mouse-speed", NULL );
    else
    {
        snprintf( value, sizeof(value), "%d", input_mouse_speed_values[index] );
        if (!launcher_kv_set( &kv, "mouse-speed", value )) return;
    }
    if (input_profile_save( &kv, save_path )) reload();
}

/* Which stop the profile's speed sits on, nearest wins. */
static int speed_choice( int speed )
{
    int best = 0, best_gap = speed > input_mouse_speed_values[0] ?
        speed - input_mouse_speed_values[0] : input_mouse_speed_values[0] - speed;

    for (int i = 1; i < INPUT_MOUSE_SPEED_COUNT; i++)
    {
        int gap = speed > input_mouse_speed_values[i] ?
            speed - input_mouse_speed_values[i] : input_mouse_speed_values[i] - speed;
        if (gap < best_gap) { best_gap = gap; best = i; }
    }
    return best;
}

/* The Input section's choice: what the controller is from this moment on. It
 * is written where every other change here goes -- the program's controls --
 * so the next run starts the same way, and handed to the runtime so this run
 * is already it. */
static void set_mode( int wanted )
{
    char value[8];

    if (wanted != INPUT_CONTROLLER && wanted != INPUT_KEYBOARD_MOUSE) return;
    snprintf( value, sizeof(value), "%d", wanted );
    if (!launcher_kv_set( &kv, "input-mode", value ) || !input_profile_save( &kv, save_path )) return;
    if (wine_nx_input_set_mode) wine_nx_input_set_mode( wanted );
    reload();
}

static int mode_row( void )
{
    return profile.mode == INPUT_KEYBOARD_MOUSE ? 1 : 0;
}

static void close_overlay( void )
{
    visible = 0;
    mode = MODE_TAB;
    capture_last = -1;
    generation++;
}

/* Entering a section: its cursor starts where the thing it edits is now. */
static void enter_tab( void )
{
    if (tab == TAB_INPUT) input_selected = mode_row();
    else if (tab == TAB_MOUSE) mouse_selected = speed_choice( profile.mouse_speed );
    generation++;
}

int wine_nx_controls_overlay_show( int show, unsigned int held, const char *root, const char *program_path )
{
    int ok = 1;

    if (show)
    {
        /* The keyboard is hidden outside this lock: it may be drawing, with
         * the picture's lock in its hands, the other way round. */
        wine_nx_osk_show( 0, held );
        pthread_mutex_lock( &overlay_lock );
        if (!visible)
        {
            snprintf( root_dir, sizeof(root_dir), "%s", root ? root : "" );
            snprintf( program, sizeof(program), "%s", program_path ? program_path : "" );
            save_path[0] = 0;
            input_profile_defaults( &profile, 1 );
            if (!(ok = reload())) {}
            else
            {
                visible = 1;
                mode = MODE_TAB;
                /* It opens where it always did: the mapping it was opened
                 * for. The other sections are one tab away. */
                tab = TAB_MAPPING;
                map_selected = map_top = 0;
                mouse_top = 0;
                pick = pick_top = 0;
                input_selected = mode_row();
                mouse_selected = speed_choice( profile.mouse_speed );
                capture_last = -1;
                ignored = held;
                previous = repeating = 0;
                touch_down = 0;
                generation++;
            }
        }
        pthread_mutex_unlock( &overlay_lock );
    }
    else
    {
        pthread_mutex_lock( &overlay_lock );
        if (visible) close_overlay();
        pthread_mutex_unlock( &overlay_lock );
    }
    return ok;
}

int wine_nx_controls_overlay_visible( void )
{
    return __atomic_load_n( &visible, __ATOMIC_RELAXED );
}

unsigned int wine_nx_controls_overlay_generation( void )
{
    return __atomic_load_n( &generation, __ATOMIC_RELAXED ) | GENERATION_BIT;
}

int wine_nx_controls_overlay_selected( void )
{
    return __atomic_load_n( &map_selected, __ATOMIC_RELAXED );
}

int wine_nx_controls_overlay_tab( void )
{
    return __atomic_load_n( &tab, __ATOMIC_RELAXED );
}

void wine_nx_controls_overlay_reset( void )
{
    pthread_mutex_lock( &overlay_lock );
    visible = 0;
    mode = MODE_TAB;
    tab = TAB_MAPPING;
    input_selected = map_selected = map_top = 0;
    mouse_selected = mouse_top = 0;
    pick = pick_top = 0;
    ignored = previous = repeating = 0;
    capture_last = -1;
    touch_down = 0;
    generation++;
    pthread_mutex_unlock( &overlay_lock );
}

static void keep_in_view( int rows )
{
    if (rows < 1) rows = 1;
    if (map_selected < 0) map_selected = 0;
    if (map_selected >= INPUT_SOURCE_COUNT) map_selected = INPUT_SOURCE_COUNT - 1;
    if (mouse_selected < 0) mouse_selected = 0;
    if (mouse_selected >= INPUT_MOUSE_SPEED_COUNT) mouse_selected = INPUT_MOUSE_SPEED_COUNT - 1;
    if (map_selected < map_top) map_top = map_selected;
    if (map_selected >= map_top + rows) map_top = map_selected - rows + 1;
    if (map_top < 0) map_top = 0;
    if (map_top > INPUT_SOURCE_COUNT - rows) map_top = INPUT_SOURCE_COUNT - rows;
    if (map_top < 0) map_top = 0;
    if (mouse_selected < mouse_top) mouse_top = mouse_selected;
    if (mouse_selected >= mouse_top + rows) mouse_top = mouse_selected - rows + 1;
    if (mouse_top < 0) mouse_top = 0;
    if (mouse_top > INPUT_MOUSE_SPEED_COUNT - rows) mouse_top = INPUT_MOUSE_SPEED_COUNT - rows;
    if (mouse_top < 0) mouse_top = 0;
    if (pick < 0) pick = 0;
    if (pick >= PICK_COUNT) pick = PICK_COUNT - 1;
    if (pick < pick_top) pick_top = pick;
    if (pick >= pick_top + PICK_COLS * PICK_ROWS) pick_top = pick - PICK_COLS * PICK_ROWS + 1;
    if (pick_top < 0) pick_top = 0;
    if (pick_top > PICK_COUNT - PICK_COLS * PICK_ROWS) pick_top = PICK_COUNT - PICK_COLS * PICK_ROWS;
    if (pick_top < 0) pick_top = 0;
}

/* Opens the row under the cursor: the grid of keys in keyboard/mouse mode,
 * the capture in controller mode. */
static void open_row( void )
{
    draft = *row_binding( map_selected );
    for (unsigned int i = 0; i < draft.count; i++) draft.code[i] = normalize_code( draft.code[i] );
    pick = 0;
    pick_top = 0;
    capture_last = -1;
    mode = controller_mapping() ? MODE_CAPTURE : MODE_PICK;
    generation++;
}

/* One step of the rows, whatever the section: the same keys everywhere, so a
 * section never has to be learned on its own. */
static void act_rows( unsigned int button, int rows )
{
    switch (button)
    {
    case OSK_UP:
        if (tab == TAB_INPUT) input_selected--;
        else if (tab == TAB_MAPPING) map_selected--;
        else mouse_selected--;
        break;
    case OSK_DOWN:
        if (tab == TAB_INPUT) input_selected++;
        else if (tab == TAB_MAPPING) map_selected++;
        else mouse_selected++;
        break;
    case OSK_ZL: case OSK_L:
        if (tab == TAB_MAPPING) map_selected -= rows;
        else if (tab == TAB_MOUSE) mouse_selected -= rows;
        break;
    case OSK_ZR: case OSK_R:
        if (tab == TAB_MAPPING) map_selected += rows;
        else if (tab == TAB_MOUSE) mouse_selected += rows;
        break;
    case OSK_A:
        if (tab == TAB_INPUT) set_mode( input_selected ? INPUT_KEYBOARD_MOUSE : INPUT_CONTROLLER );
        else if (tab == TAB_MAPPING) open_row();
        else { save_speed( mouse_selected ); generation++; }
        break;
    case OSK_X:
        if (tab == TAB_MAPPING) save_row( map_selected, &(struct input_binding){0} );
        break;
    case OSK_Y:
        if (tab == TAB_MAPPING) save_row( map_selected, NULL );
        else if (tab == TAB_MOUSE) { save_speed( -1 ); generation++; }
        break;
    }
    if (input_selected < 0) input_selected = 0;
    if (input_selected > 1) input_selected = 1;
    keep_in_view( rows );
}

static void act_section( unsigned int button, int rows )
{
    switch (button)
    {
    case OSK_LEFT:
        tab = (tab + TAB_COUNT - 1) % TAB_COUNT;
        enter_tab();
        keep_in_view( rows );
        break;
    case OSK_RIGHT:
        tab = (tab + 1) % TAB_COUNT;
        enter_tab();
        keep_in_view( rows );
        break;
    case OSK_B: case OSK_MINUS: case OSK_PLUS:
        close_overlay();
        break;
    }
}

static void act_pick( unsigned int button )
{
    switch (button)
    {
    case OSK_UP: pick -= PICK_COLS; break;
    case OSK_DOWN: pick += PICK_COLS; break;
    case OSK_LEFT: pick--; break;
    case OSK_RIGHT: pick++; break;
    case OSK_ZL: case OSK_L: pick -= PICK_COLS * PICK_ROWS; break;
    case OSK_ZR: case OSK_R: pick += PICK_COLS * PICK_ROWS; break;
    case OSK_A:
    {
        unsigned short code = pick_targets[pick].code;
        unsigned int i;

        for (i = 0; i < draft.count; i++) if (normalize_code( draft.code[i] ) == code) break;
        if (i < draft.count)
            memmove( draft.code + i, draft.code + i + 1, (--draft.count - i) * sizeof(draft.code[0]) );
        else if (draft.count < INPUT_CHORD_MAX) draft.code[draft.count++] = code;
        generation++;
        break;
    }
    case OSK_X: draft.count = 0; generation++; break;
    case OSK_B: save_row( map_selected, &draft ); mode = MODE_TAB; generation++; break;
    case OSK_MINUS: case OSK_PLUS: close_overlay(); break;
    }
    if (pick < 0) pick = 0;
    if (pick >= PICK_COUNT) pick = PICK_COUNT - 1;
}

/* The row a capture would assign: the first button pressed, or the stick
 * direction pushed furthest. Minus is the overlay's own (it opens and closes
 * it) and B and X are kept for cancelling and clearing, as the hints say;
 * nothing is taken while Minus is held. */
static int captured_source( unsigned int held, int left_x, int left_y, int right_x, int right_y )
{
    const struct { int value; int source; } directions[] =
    {
        { left_y, 16 }, { -left_y, 17 }, { -left_x, 18 }, { left_x, 19 },
        { right_y, 20 }, { -right_y, 21 }, { -right_x, 22 }, { right_x, 23 },
    };
    int best = -1, best_value = STICK_DEAD;

    if (held & (OSK_MINUS | OSK_B | OSK_X)) return -1;
    for (unsigned int bit = 0; bit < 16; bit++)
        if (held & (1u << bit)) return button_source[bit];
    for (unsigned int i = 0; i < sizeof(directions) / sizeof(directions[0]); i++)
        if (directions[i].value > best_value) { best_value = directions[i].value; best = directions[i].source; }
    return best;
}

static void act_capture( unsigned int held, unsigned int pressed, int left_x, int left_y,
                         int right_x, int right_y )
{
    int source = captured_source( held, left_x, left_y, right_x, right_y );

    if (pressed & OSK_B) { mode = MODE_TAB; generation++; return; }
    if (pressed & OSK_X) { save_row( map_selected, &(struct input_binding){0} ); mode = MODE_TAB; generation++; return; }
    if (source >= 0 && source != capture_last)
    {
        /* The captured button is the pad output the row should make: the
         * same button, which is its own target code in the pad table. */
        save_row( map_selected, &(struct input_binding){ { (unsigned short)(source + 1) }, 1 } );
        mode = MODE_TAB;
        generation++;
    }
    capture_last = source;
}

static int repeats( unsigned int button )
{
    return button & (OSK_UP | OSK_DOWN | OSK_ZL | OSK_ZR | OSK_L | OSK_R);
}

/* A finger on the console's panel, in the pixels of the picture: the panel is
 * 1280x720 and the presenters may draw the overlay for anything, so the touch
 * is scaled through the screen it was last drawn for. */
static int touch_x_on_screen( int touch_x, int screen_w )
{
    if (screen_w <= 0 || screen_w == TOUCH_PANEL_W) return touch_x;
    return (int)((long long)touch_x * screen_w / TOUCH_PANEL_W);
}

static int touch_y_on_screen( int touch_y, int screen_h )
{
    if (screen_h <= 0 || screen_h == TOUCH_PANEL_H) return touch_y;
    return (int)((long long)touch_y * screen_h / TOUCH_PANEL_H);
}

/* A tap is a press where the finger comes up, on the row, cell or tab it went
 * down on. The geometry is the drawn one: the same layout() the picture is
 * made with, for the screen it was made for, so a tap lands on what it looks
 * like it lands on at every resolution. */
static int touch_panel( struct geometry *g, int touching, int touch_x, int touch_y, int *on_panel )
{
    int x = touch_x_on_screen( touch_x, g->screen_w );
    int y = touch_y_on_screen( touch_y, g->screen_h );
    int lx = x - g->x, ly = y - g->y;
    int footer_top = g->height - g->pad - g->footer;

    *on_panel = 0;
    if (!touching) { touch_down = 0; return 0; }
    *on_panel = lx >= 0 && ly >= 0 && lx < g->width && ly < g->height;
    if (!*on_panel) { touch_down = 0; return 0; }
    if (touch_down) return 1;
    touch_down = 1;

    if (ly >= g->pad + g->header && ly < g->pad + g->header + g->tabs)
    {
        int index = (lx - g->pad) / (g->tab_w > 0 ? g->tab_w : 1);

        if (lx >= g->pad && index >= 0 && index < TAB_COUNT)
        {
            if (mode != MODE_TAB || tab != index)
            {
                mode = MODE_TAB;
                tab = index;
                enter_tab();
            }
        }
        return 1;
    }
    if (ly >= footer_top)
    {
        /* The hint strip is Back, whatever else the section is doing. */
        if (mode == MODE_PICK) act_pick( OSK_B );
        else if (mode == MODE_CAPTURE) { mode = MODE_TAB; generation++; }
        else close_overlay();
        return 1;
    }
    if (ly < g->content_top || ly >= g->content_top + g->content_h) return 1;

    if (mode == MODE_PICK)
    {
        int cell_w, cell_h, offset, col, row, index;

        grid_metrics( g, &cell_w, &cell_h, &offset );
        if (ly < g->content_top + offset) return 1;   /* the band above the grid */
        col = (lx - g->pad) / (cell_w + g->gap);
        row = (ly - g->content_top - offset) / (cell_h + g->gap);
        index = pick_top + row * PICK_COLS + col;
        if (row < 0 || row >= PICK_ROWS || col < 0 || col >= PICK_COLS ||
            lx < g->pad || lx >= g->pad + PICK_COLS * (cell_w + g->gap) - g->gap ||
            index < 0 || index >= PICK_COUNT) return 1;
        pick = index;
        act_pick( OSK_A );
        return 1;
    }
    if (mode == MODE_CAPTURE) return 1;

    {
        int row = (ly - g->content_top) / (g->row_h + g->gap);

        if (row < 0 || row >= g->rows) return 1;
        if (tab == TAB_INPUT)
        {
            if (row > 1) return 1;
            input_selected = row;
            act_rows( OSK_A, g->rows );
        }
        else if (tab == TAB_MAPPING && map_top + row < INPUT_SOURCE_COUNT)
        {
            map_selected = map_top + row;
            act_rows( OSK_A, g->rows );
        }
        else if (tab == TAB_MOUSE && mouse_top + row < INPUT_MOUSE_SPEED_COUNT)
        {
            mouse_selected = mouse_top + row;
            act_rows( OSK_A, g->rows );
        }
    }
    return 1;
}

int wine_nx_controls_overlay_input( unsigned int held, int left_x, int left_y, int right_x, int right_y,
                                    int touching, int touch_x, int touch_y, uint64_t now_ns )
{
    struct geometry g;
    unsigned int pressed, bit;
    int on_panel = 0;

    pthread_mutex_lock( &overlay_lock );
    if (!visible)
    {
        pthread_mutex_unlock( &overlay_lock );
        return 0;
    }
    layout( touch_screen_w, touch_screen_h, &g );
    if (mode != MODE_CAPTURE)
    {
        if (left_y > STICK_DEAD) held |= OSK_UP;
        if (left_y < -STICK_DEAD) held |= OSK_DOWN;
        if (left_x < -STICK_DEAD) held |= OSK_LEFT;
        if (left_x > STICK_DEAD) held |= OSK_RIGHT;
    }
    ignored &= held;
    held &= ~ignored;
    pressed = held & ~previous;
    previous = held;

    if (mode == MODE_CAPTURE)
        act_capture( held, pressed, left_x, left_y, right_x, right_y );
    else
    {
        for (bit = 1; bit <= OSK_STICKR && visible; bit <<= 1)
        {
            if (!(pressed & bit)) continue;
            /* The sections are Left and Right; a row of one of them is opened
             * with A; a row's grid keeps Left and Right for its cells. */
            if (mode == MODE_TAB && (bit & (OSK_LEFT | OSK_RIGHT))) act_section( bit, g.rows );
            else if (mode == MODE_TAB && (bit & (OSK_B | OSK_MINUS | OSK_PLUS))) act_section( bit, g.rows );
            else if (mode == MODE_TAB) act_rows( bit, g.rows );
            else act_pick( bit );
            if (visible && mode != MODE_CAPTURE && repeats( bit ))
            {
                repeating = bit;
                repeat_at = now_ns + REPEAT_FIRST_NS;
            }
        }
        if (repeating && !(held & repeating)) repeating = 0;
        if (visible && mode != MODE_CAPTURE && repeating && now_ns >= repeat_at)
        {
            if (mode == MODE_TAB && (repeating & (OSK_LEFT | OSK_RIGHT))) repeating = 0;
            else if (mode == MODE_TAB) act_rows( repeating, g.rows );
            else act_pick( repeating );
            repeat_at = now_ns + REPEAT_NEXT_NS;
        }
    }

    layout( touch_screen_w, touch_screen_h, &g );
    if (visible) touch_panel( &g, touching, touch_x, touch_y, &on_panel );
    keep_in_view( g.rows );
    pthread_mutex_unlock( &overlay_lock );
    return on_panel;
}

/* Drawing. */

#define PANEL    0xff14161bu
#define BORDER   0xff2a2f3au
#define CARD     0xff1e222bu
#define CARD_ON  0xff2a3542u
#define ACCENT   0xff3d9cf5u
#define TEXT     0xffeef1f6u
#define DIM      0xff96a0b0u

static TTF_Font *fonts[2];
static int font_sizes[2], font_failed;
static struct { char text[80]; int size; SDL_Surface *surface; } labels[320];
static int label_count;

static TTF_Font *font_at( int which, int size )
{
    const void *data;
    size_t bytes;

    if (fonts[which] && font_sizes[which] == size) return fonts[which];
    if (font_failed) return NULL;
    if (!TTF_WasInit() && TTF_Init()) { font_failed = 1; return NULL; }
    if (!wine_nx_osk_font( &data, &bytes )) { font_failed = 1; return NULL; }
    if (fonts[which]) TTF_CloseFont( fonts[which] );
    fonts[which] = TTF_OpenFontRW( SDL_RWFromConstMem( data, (int)bytes ), 1, size );
    font_sizes[which] = size;
    return fonts[which];
}

/* White text, drawn once for each size it is asked for. */
static SDL_Surface *label_surface( int which, int size, const char *text )
{
    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Surface *drawn, *converted;
    TTF_Font *font;
    int i;

    for (i = 0; i < label_count; i++)
        if (labels[i].size == size * 2 + which && !strcmp( labels[i].text, text )) return labels[i].surface;
    if (!(font = font_at( which, size )) || !(drawn = TTF_RenderUTF8_Blended( font, text, white ))) return NULL;
    converted = SDL_ConvertSurfaceFormat( drawn, SDL_PIXELFORMAT_ARGB8888, 0 );
    SDL_FreeSurface( drawn );
    if (!converted) return NULL;
    if (label_count == sizeof(labels) / sizeof(labels[0]))
    {
        for (i = 0; i < label_count; i++) SDL_FreeSurface( labels[i].surface );
        label_count = 0;
    }
    snprintf( labels[label_count].text, sizeof(labels[0].text), "%s", text );
    labels[label_count].size = size * 2 + which;
    labels[label_count].surface = converted;
    label_count++;
    return converted;
}

static uint32_t blend( uint32_t under, uint32_t color, unsigned int alpha )
{
    uint32_t r = ((under >> 16 & 0xff) * (255 - alpha) + (color >> 16 & 0xff) * alpha) / 255;
    uint32_t g = ((under >> 8 & 0xff) * (255 - alpha) + (color >> 8 & 0xff) * alpha) / 255;
    uint32_t b = ((under & 0xff) * (255 - alpha) + (color & 0xff) * alpha) / 255;
    return 0xff000000u | r << 16 | g << 8 | b;
}

static void fill_rounded( int x0, int y0, int w, int h, int radius, uint32_t color )
{
    int x, y;

    if (radius < 0) radius = 0;
    for (y = y0 < 0 ? 0 : y0; y < y0 + h && y < picture_h; y++)
        for (x = x0 < 0 ? 0 : x0; x < x0 + w && x < picture_w; x++)
        {
            int dx = x < x0 + radius ? x0 + radius - x : x >= x0 + w - radius ? x - (x0 + w - radius - 1) : 0;
            int dy = y < y0 + radius ? y0 + radius - y : y >= y0 + h - radius ? y - (y0 + h - radius - 1) : 0;
            float d = sqrtf( (float)(dx * dx + dy * dy) );

            if (d <= radius - 0.5f) picture[y * picture_w + x] = color;
            else if (d < radius + 0.5f) picture[y * picture_w + x] = blend( picture[y * picture_w + x], color,
                                                                            (unsigned int)((radius + 0.5f - d) * 255) );
        }
}

/* Text, baseline-centred on y: to the left of x (align 0), around x (1), or
 * ending at x (2). */
static void text_at( int which, int size, const char *text, int x, int y, uint32_t color, int align )
{
    SDL_Surface *s = label_surface( which, size, text );
    int px, py, x0, y0;

    if (!s) return;
    x0 = align == 1 ? x - s->w / 2 : align == 2 ? x - s->w : x;
    y0 = y - s->h / 2;
    for (py = 0; py < s->h; py++)
        for (px = 0; px < s->w; px++)
        {
            uint32_t p = ((uint32_t *)((uint8_t *)s->pixels + py * s->pitch))[px];
            int x1 = x0 + px, y1 = y0 + py;

            if ((p >> 24) && x1 >= 0 && y1 >= 0 && x1 < picture_w && y1 < picture_h)
                picture[y1 * picture_w + x1] = blend( picture[y1 * picture_w + x1], color, p >> 24 );
        }
}

/* One row of a section: its name, and what it is now. `cursor` is the row the
 * section's selection is on, `chosen` what the row is set to. */
static void draw_row( const struct geometry *g, int row, const char *label, const char *value,
                      int cursor, int chosen )
{
    int x = g->x + g->pad;
    int y = g->content_y + row * (g->row_h + g->gap);
    int w = g->width - 2 * g->pad, h = g->row_h;
    int radius = h / 4;

    int room = (int)( w * (value && *value ? 0.56f : 0.86f) );
    int size = g->text;

    fill_rounded( x, y, w, h, radius, cursor ? CARD_ON : CARD );
    if (chosen) fill_rounded( x, y, h / 6 > 2 ? h / 6 : 2, h, 2, ACCENT );
    /* Room for the name before the value is asked for it: a name too long for
     * it is set smaller rather than written over what it names. */
    while (size > g->tiny)
    {
        SDL_Surface *drawn = label_surface( 0, size, label );

        if (!drawn || drawn->w <= room) break;
        size--;
    }
    text_at( 0, size, label, x + h / 3, y + h / 2, TEXT, 0 );
    if (value && *value) text_at( 1, g->small, value, x + w - h / 3, y + h / 2, cursor ? TEXT : DIM, 2 );
}

static void put_footer( const struct geometry *g, const char *hint )
{
    text_at( 1, g->small, hint, g->x + g->width / 2, g->y + g->height - g->pad - g->footer / 2, DIM, 1 );
}

/* The panel's own furniture: its title, the section the one on the right is,
 * and the tabs. `active` is the tab drawn selected. */
static void draw_chrome( const struct geometry *g, const char *title, const char *right, int active )
{
    int index;

    text_at( 0, g->text, title, g->x + g->pad + g->header / 3, g->y + g->pad + g->header / 2, TEXT, 0 );
    if (right && *right)
        text_at( 1, g->small, right, g->x + g->width - g->pad - g->header / 3,
                 g->y + g->pad + g->header / 2, DIM, 2 );
    for (index = 0; index < TAB_COUNT; index++)
    {
        int x = g->x + g->pad + index * g->tab_w;
        int y = g->y + g->pad + g->header + g->gap / 2;
        int w = g->tab_w - g->gap;
        int h = g->tabs - g->gap;
        int on = index == active;

        if (w <= 0 || h <= 0) continue;
        fill_rounded( x, y, w, h, h / 3, on ? ACCENT : CARD );
        text_at( 0, g->small, tab_labels[index], x + w / 2, y + h / 2, on ? PANEL : DIM, 1 );
    }
}

static void draw_input( const struct geometry *g )
{
    static const char *notes[2] =
    {
        /* input_mode_labels' order: the controller, then the keyboard. */
        "the game reads the pad",
        "the pad stands in for the keys",
    };
    int row;

    for (row = 0; row < 2; row++)
        draw_row( g, row, input_mode_labels[row], notes[row], row == input_selected,
                  profile.mode == (row == 0 ? INPUT_CONTROLLER : INPUT_KEYBOARD_MOUSE) );
    /* Under them, what the choice means where the game is: the same swap the
     * two buttons make, written down where the rest of this menu writes. */
    {
        int x = g->x + g->pad + g->gap;
        int y = g->content_y + 2 * (g->row_h + g->gap) + g->row_h / 2;
        int step = g->small + g->gap;

        text_at( 0, g->small, "+ and - tapped together do the same", x, y, DIM, 0 );
        text_at( 0, g->small, "while a game is on.", x, y + step, DIM, 0 );
        text_at( 0, g->small, "It is saved with this game's controls,", x, y + 3 * step, DIM, 0 );
        text_at( 0, g->small, "so the next run starts the same way.", x, y + 4 * step, DIM, 0 );
    }
    put_footer( g, "A use    L / R section    B close" );
}

static void draw_mapping( const struct geometry *g )
{
    char label[192];
    int row;

    for (row = 0; row < g->rows; row++)
    {
        int source = map_top + row;

        if (source >= INPUT_SOURCE_COUNT) break;
        input_binding_label( row_binding( source ), controller_mapping(), label, sizeof(label) );
        draw_row( g, row, input_sources[source].label, label, source == map_selected, 0 );
    }
    put_footer( g, "A open   X clear   Y reset   ZL/ZR page   B close" );
}

static void draw_mouse( const struct geometry *g )
{
    char label[16];
    int row;

    for (row = 0; row < g->rows; row++)
    {
        int index = mouse_top + row;

        if (index >= INPUT_MOUSE_SPEED_COUNT) break;
        snprintf( label, sizeof(label), "%d%%", input_mouse_speed_values[index] );
        draw_row( g, row, label, profile.mouse_speed == input_mouse_speed_values[index] ? "in use" : NULL,
                  index == mouse_selected, profile.mouse_speed == input_mouse_speed_values[index] );
    }
    put_footer( g, "A set    Y the game's own    B close" );
}

static void draw_pick( const struct geometry *g )
{
    int cell_w, cell_h, offset, row, col;

    grid_metrics( g, &cell_w, &cell_h, &offset );
    for (row = 0; row < PICK_ROWS; row++)
        for (col = 0; col < PICK_COLS; col++)
        {
            int index = pick_top + row * PICK_COLS + col;
            int x, y;

            if (index >= PICK_COUNT) continue;
            x = g->x + g->pad + col * (cell_w + g->gap);
            y = g->content_y + offset + row * (cell_h + g->gap);
            if (index == pick) fill_rounded( x - 2, y - 2, cell_w + 4, cell_h + 4, cell_h / 3, DIM );
            fill_rounded( x, y, cell_w, cell_h, cell_h / 3,
                          has_code( &draft, pick_targets[index].code ) ? ACCENT : CARD );
            text_at( 0, g->tiny, pick_targets[index].label, x + cell_w / 2, y + cell_h / 2,
                     has_code( &draft, pick_targets[index].code ) ? PANEL : DIM, 1 );
        }
    put_footer( g, "A add / remove   ZL/ZR page   B done" );
}

static void draw_capture( const struct geometry *g )
{
    char label[192];

    input_binding_label( &draft, controller_mapping(), label, sizeof(label) );
    text_at( 1, g->small, label, g->x + g->width / 2, g->content_y + g->content_h / 3, DIM, 1 );
    text_at( 1, g->text, "Press the button", g->x + g->width / 2, g->content_y + g->content_h / 2, TEXT, 1 );
    text_at( 1, g->small, "to assign it to this row", g->x + g->width / 2,
             g->content_y + g->content_h / 2 + g->text + g->gap, DIM, 1 );
    put_footer( g, "X clear    B cancel" );
}

static void speed_text( char *out, size_t size )
{
    if (profile.mouse_speed_explicit) snprintf( out, size, "%d%%", profile.mouse_speed );
    else snprintf( out, size, "%d%% (default)", profile.mouse_speed );
}

static const char *mode_text( void )
{
    return input_mode_labels[controller_mapping() ? 0 : 1];
}

static void draw( int screen_w, int screen_h )
{
    struct geometry g;
    char title[128], right[128], speed[48];
    int x, y;

    layout( screen_w, screen_h, &g );
    if (picture_w != g.width || picture_h != g.height || !picture)
    {
        free( picture );
        picture_w = g.width;
        picture_h = g.height;
        picture = malloc( (size_t)picture_w * picture_h * 4 );
    }
    if (!picture) return;
    for (y = 0; y < picture_h; y++)
        for (x = 0; x < picture_w; x++)
            picture[y * picture_w + x] = (x == 0 || y == 0 || x == picture_w - 1 || y == picture_h - 1) ? BORDER : PANEL;

    if (mode == MODE_PICK || mode == MODE_CAPTURE)
    {
        snprintf( title, sizeof(title), "Map %s", input_sources[map_selected].label );
        if (mode == MODE_PICK) input_binding_label( &draft, controller_mapping(), right, sizeof(right) );
        else snprintf( right, sizeof(right), "%s", "listening" );
        draw_chrome( &g, title, right, TAB_MAPPING );
        if (mode == MODE_PICK) draw_pick( &g );
        else draw_capture( &g );
        return;
    }

    switch (tab)
    {
    case TAB_INPUT:
        draw_chrome( &g, "Input", mode_text(), TAB_INPUT );
        draw_input( &g );
        break;
    case TAB_MOUSE:
        speed_text( speed, sizeof(speed) );
        draw_chrome( &g, "Mouse", speed, TAB_MOUSE );
        draw_mouse( &g );
        break;
    default:
        draw_chrome( &g, "Mapping", mode_text(), TAB_MAPPING );
        draw_mapping( &g );
        break;
    }
}

/* Draws the picture for that screen if what is there is not it. */
static void ensure_picture( int screen_w, int screen_h )
{
    if (touch_screen_w != screen_w || touch_screen_h != screen_h)
    {
        /* Another screen size is another place for a finger as well: the
         * touches come in on the console's own panel, scaled into this. */
        touch_screen_w = screen_w;
        touch_screen_h = screen_h;
        generation++;
    }
    if (picture && picture_generation == generation && picture_screen_w == screen_w && picture_screen_h == screen_h)
        return;
    if (picture_screen_w != screen_w || picture_screen_h != screen_h)
    {
        /* Another screen size is another picture: the presenters that copied
         * the old one see a new generation. */
        picture_screen_w = screen_w;
        picture_screen_h = screen_h;
    }
    draw( screen_w, screen_h );
    picture_generation = generation;
}

int wine_nx_controls_overlay_frame( int screen_width, int screen_height, struct wine_nx_osk_frame *frame )
{
    struct geometry g;

    pthread_mutex_lock( &overlay_lock );
    if (!visible)
    {
        pthread_mutex_unlock( &overlay_lock );
        return 0;
    }
    ensure_picture( screen_width, screen_height );
    layout( screen_width, screen_height, &g );
    frame->x = g.x;
    frame->y = g.y;
    frame->width = g.width;
    frame->height = g.height;
    frame->generation = generation | GENERATION_BIT;
    pthread_mutex_unlock( &overlay_lock );
    return 1;
}

unsigned int wine_nx_controls_overlay_copy( int screen_width, int screen_height, void *pixels,
                                            int stride, int rgba )
{
    unsigned int copied = 0;
    int x, y;

    pthread_mutex_lock( &overlay_lock );
    if (visible)
    {
        ensure_picture( screen_width, screen_height );
        for (y = 0; y < picture_h; y++)
        {
            uint32_t *out = (uint32_t *)((uint8_t *)pixels + (size_t)y * stride);
            const uint32_t *in = picture + (size_t)y * picture_w;

            if (!rgba) memcpy( out, in, picture_w * 4 );
            else
                for (x = 0; x < picture_w; x++)
                    out[x] = (in[x] & 0xff00ff00u) | (in[x] >> 16 & 0xff) | (in[x] & 0xff) << 16;
        }
        copied = generation | GENERATION_BIT;
    }
    pthread_mutex_unlock( &overlay_lock );
    return copied;
}
