#include <string.h>

#include "launcher_preset.h"

/* The settings a bundle owns. The rest of struct launcher_settings is not a
 * preset's to change: the renderer, the upscaler, frame generation and the
 * game's own title and arguments stay where the user put them. */
struct launcher_preset_fields
{
    int translator;       /* -1: the library's own choice; 0 Box64, 1 FEX, 2 tuned FEX */
    int four_cores;
    int lazy_file_views;
    int fex_disk_cache;   /* the translator's code kept on the card between runs */
    int fast_sync;
    int vsync;
    int frame_limit;      /* index into launcher_frame_limits */
};

static const struct launcher_preset_fields launcher_presets[LAUNCHER_PRESET_COUNT] =
{
    /* Balanced is exactly what launcher_preset_defaults() leaves behind, so a
     * game that has never been configured says so instead of "Custom". */
    { -1, 1, 0, 0, 0, 1, 0 },
    /* Performance: the tuned translator and every fast path, uncapped --
     * including the code cache, which is what makes a second run of a game
     * start without translating everything again. */
    { 2, 1, 1, 1, 1, 0, 0 },
    /* Battery: the fast paths with a 30 fps cap, which is the frame rate a
     * handheld can hold for hours. */
    { 2, 1, 1, 1, 1, 1, 1 },
    /* Compatibility: slow and safe, with the translator left to the library
     * and nothing experimental on. */
    { -1, 0, 0, 0, 0, 1, 0 }
};

static const char *const launcher_preset_labels[LAUNCHER_PRESET_COUNT] =
{
    "Balanced", "Performance", "Battery", "Compatibility"
};

static const char *const launcher_preset_helps[LAUNCHER_PRESET_COUNT] =
{
    "What a game that has never been configured runs with: the translator the "
    "library picks, 4-core support, standard synchronization, vsync and the "
    "translator's code cache off.",
    "The tuned FEX when it is installed, 4-core support, fast file loading, "
    "the translated code cache, Horizon synchronization, vsync off and no "
    "frame cap.",
    "The tuned FEX when it is installed, 4-core support, fast file loading, "
    "the translated code cache, Horizon synchronization, vsync on and a 30 fps "
    "cap: less power, longer play.",
    "Standard synchronization, no fast file loading and no 4-core split, for a "
    "game that misbehaves with the fast paths."
};

static const char *const launcher_preset_custom_help =
    "This game's settings are its own. A preset sets the translator, the fast "
    "paths, the translated code cache and frame pacing at once; the renderer "
    "and upscaling stay as they are.";

/* What the wanted translator becomes on this card. Returns 0 when the program
 * has no translator to choose and the preset must leave the choice alone. */
static int launcher_preset_translator( int wanted, int translators, int *chosen )
{
    if (!(translators & LAUNCHER_TRANSLATOR_ANY)) return 0;
    if (wanted < 0)
    {
        *chosen = -1;
        return 1;
    }
    if (wanted >= 2 && (translators & LAUNCHER_TRANSLATOR_FEX_OPT))
    {
        *chosen = 2;
        return 1;
    }
    if (wanted >= 1 && (translators & LAUNCHER_TRANSLATOR_FEX))
    {
        *chosen = 1;
        return 1;
    }
    if (translators & LAUNCHER_TRANSLATOR_BOX64)
    {
        *chosen = 0;
        return 1;
    }
    return 0;
}

const char *launcher_preset_label( int preset )
{
    return preset >= 0 && preset < LAUNCHER_PRESET_COUNT ? launcher_preset_labels[preset] : "Custom";
}

const char *launcher_preset_help( int preset )
{
    return preset >= 0 && preset < LAUNCHER_PRESET_COUNT ? launcher_preset_helps[preset] :
                                                           launcher_preset_custom_help;
}

void launcher_preset_defaults( struct launcher_settings *settings )
{
    memset( settings, 0, sizeof(*settings) );
    /* Every field a settings file can leave out, at the value its reader gives
     * it when the key is missing. */
    settings->verbose = settings->profile = settings->framebuffer = -1;
    settings->fex = -1;
    settings->four_cores = 1;
    settings->d3d = LAUNCHER_D3D_DXVK;
    settings->dxvk_source = DXVK_SOURCE_OFFICIAL;
    settings->vsync = 1;
    settings->lsfg_performance = 1;
    settings->lsfg_flow = 1;
    settings->upscaling_sharpness = 2;
    settings->scaling = SCREEN_SCALING_ASPECT;
}

int launcher_preset_apply( struct launcher_settings *settings, int preset, int translators )
{
    const struct launcher_preset_fields *fields;
    struct launcher_settings after;
    int translator;

    if (preset < 0 || preset >= LAUNCHER_PRESET_COUNT) return 0;
    fields = &launcher_presets[preset];
    after = *settings;
    after.four_cores = fields->four_cores;
    after.lazy_file_views = fields->lazy_file_views;
    after.fex_disk_cache = fields->fex_disk_cache;
    after.fast_sync = fields->fast_sync;
    after.vsync = fields->vsync;
    after.frame_limit = fields->frame_limit;
    if (launcher_preset_translator( fields->translator, translators, &translator )) after.fex = translator;
    /* The fields it owns, compared one by one: the struct has padding, and a
     * byte comparison would say a bundle changed a setting it did not. */
    if (settings->four_cores == after.four_cores && settings->lazy_file_views == after.lazy_file_views &&
        settings->fex_disk_cache == after.fex_disk_cache && settings->fast_sync == after.fast_sync &&
        settings->vsync == after.vsync && settings->frame_limit == after.frame_limit &&
        settings->fex == after.fex) return 0;
    *settings = after;
    return 1;
}

int launcher_preset_match( const struct launcher_settings *settings, int translators )
{
    int i;

    for (i = 0; i < LAUNCHER_PRESET_COUNT; i++)
    {
        const struct launcher_preset_fields *fields = &launcher_presets[i];
        int translator;

        if (settings->four_cores != fields->four_cores || settings->lazy_file_views != fields->lazy_file_views ||
            settings->fex_disk_cache != fields->fex_disk_cache || settings->fast_sync != fields->fast_sync ||
            settings->vsync != fields->vsync || settings->frame_limit != fields->frame_limit ) continue;
        if (launcher_preset_translator( fields->translator, translators, &translator ) &&
            settings->fex != translator) continue;
        return i;
    }
    return LAUNCHER_PRESET_CUSTOM;
}
