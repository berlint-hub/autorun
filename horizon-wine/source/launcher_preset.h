/*
 * The bundles a game's settings can be set to in one go, the way a handheld's
 * per-game profiles work: instead of going through the translator, the fast
 * paths, the frame limiter and vsync one by one, one row sets them together.
 *
 * A preset only touches the emulation and frame pacing. The renderer, the
 * upscaler, frame generation, the title and the arguments are the game's own
 * choices and are left as they are.
 */
#ifndef WINE_NX_LAUNCHER_PRESET_H
#define WINE_NX_LAUNCHER_PRESET_H

#include "launcher_settings.h"

/* LAUNCHER_PRESET_CUSTOM is what a game whose settings are its own gets. */
enum launcher_preset
{
    LAUNCHER_PRESET_CUSTOM = -1,
    LAUNCHER_PRESET_BALANCED,
    LAUNCHER_PRESET_PERFORMANCE,
    LAUNCHER_PRESET_BATTERY,
    LAUNCHER_PRESET_COMPATIBILITY,
    LAUNCHER_PRESET_COUNT
};

/* Which CPU translators this build can run the program with. A preset that
 * asks for one that is not installed falls back to the next one down, so a
 * card with the stock FEX modules only gets FEX and the tuned module is used
 * once it is installed. No bit at all means there is nothing to choose -- the
 * program is not an x86 or x64 one, or this build has no FEX -- and the
 * translator is then left alone.
 */
enum launcher_translator
{
    LAUNCHER_TRANSLATOR_BOX64 = 1 << 0,
    LAUNCHER_TRANSLATOR_FEX = 1 << 1,
    LAUNCHER_TRANSLATOR_FEX_OPT = 1 << 2,
    LAUNCHER_TRANSLATOR_ANY = LAUNCHER_TRANSLATOR_BOX64 | LAUNCHER_TRANSLATOR_FEX |
                              LAUNCHER_TRANSLATOR_FEX_OPT
};

/* "Balanced", "Performance", "Battery", "Compatibility"; "Custom" for -1. */
const char *launcher_preset_label( int preset );
/* What the bundle is for, shown under its row. */
const char *launcher_preset_help( int preset );

/* What a program that has never been configured runs with: the defaults its
 * settings file is read against when it leaves a key out. */
void launcher_preset_defaults( struct launcher_settings *settings );

/* Sets the bundle in place, leaving everything it does not own as it is.
 * Returns 1 when a setting changed. */
int launcher_preset_apply( struct launcher_settings *settings, int preset, int translators );

/* Which bundle these settings are, or LAUNCHER_PRESET_CUSTOM. What apply()
 * would set is what is compared, so the two agree whatever is installed. */
int launcher_preset_match( const struct launcher_settings *settings, int translators );

#endif
