/* Host test for the launcher's per-game presets (source/launcher_preset.c):
 * what each bundle sets, how it falls back on a card that has only the stock
 * translators, and that a game whose settings were never touched reads as the
 * Balanced preset rather than as Custom. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../source/launcher_preset.h"

static void test_defaults_match_an_empty_file(void)
{
    struct launcher_kv kv;
    struct launcher_settings read, defaults;

    /* An empty settings file: every key is missing, and the reader says what a
     * program that has never been configured runs with. */
    kv.size = 0;
    kv.text[0] = 0;
    launcher_settings_read( &kv, &read );
    launcher_preset_defaults( &defaults );

    assert( !strcmp( read.title, defaults.title ) && !read.title[0] );
    assert( read.hidden == defaults.hidden && read.verbose == defaults.verbose );
    assert( read.profile == defaults.profile && read.fast_sync == defaults.fast_sync );
    assert( read.fex == defaults.fex && read.four_cores == defaults.four_cores );
    assert( read.lazy_file_views == defaults.lazy_file_views );
    assert( read.framebuffer == defaults.framebuffer && read.scaling == defaults.scaling );
    assert( read.d3d == defaults.d3d && read.dxvk_source == defaults.dxvk_source );
    assert( read.dxvk_hud == defaults.dxvk_hud && read.frame_limit == defaults.frame_limit );
    assert( read.vsync == defaults.vsync && read.resolution == defaults.resolution );
    assert( read.voodoo == defaults.voodoo && read.voodoo_api == defaults.voodoo_api );
    assert( read.voodoo_scaling == defaults.voodoo_scaling );
    assert( read.voodoo_resolution == defaults.voodoo_resolution );
    assert( read.voodoo_msaa == defaults.voodoo_msaa );
    assert( read.lsfg_enabled == defaults.lsfg_enabled );
    assert( read.lsfg_performance == defaults.lsfg_performance && read.lsfg_flow == defaults.lsfg_flow );
    assert( read.upscaling == defaults.upscaling );
    assert( read.upscaling_sharpness == defaults.upscaling_sharpness );

    /* The defaults are what the reader makes of a file without keys: the two
     * cannot drift apart without this failing. */
    assert( !memcmp( &read, &defaults, sizeof(read) ) );
}

static void test_apply_leaves_what_it_does_not_own(void)
{
    struct launcher_settings settings, before;

    launcher_preset_defaults( &settings );
    snprintf( settings.title, sizeof(settings.title), "Need for Speed" );
    settings.hidden = 1;
    settings.verbose = 1;
    settings.d3d = LAUNCHER_D3D_WINE;
    settings.upscaling = 2;
    settings.upscaling_sharpness = 4;
    settings.lsfg_enabled = 1;
    settings.lsfg_flow = 2;
    settings.dxvk_hud = 2;
    settings.resolution = 3;
    settings.voodoo = 1;
    settings.voodoo_msaa = 2;
    before = settings;

    assert( launcher_preset_apply( &settings, LAUNCHER_PRESET_PERFORMANCE, LAUNCHER_TRANSLATOR_ANY ) );
    assert( !strcmp( settings.title, "Need for Speed" ) && settings.hidden == 1 && settings.verbose == 1 );
    assert( settings.d3d == LAUNCHER_D3D_WINE && settings.upscaling == 2 );
    assert( settings.upscaling_sharpness == 4 && settings.lsfg_enabled == 1 && settings.lsfg_flow == 2 );
    assert( settings.dxvk_hud == 2 && settings.resolution == 3 && settings.voodoo == 1 );
    assert( settings.voodoo_msaa == 2 );

    /* What it does own, and what Performance is. */
    assert( settings.fex == 2 && settings.four_cores == 1 && settings.lazy_file_views == 1 );
    /* The code cache is not a preset's: it stays where the game's owner put
     * it, off unless they turned it on. */
    assert( settings.fex_disk_cache == 0 );
    assert( settings.fast_sync == 1 && settings.vsync == 0 && settings.frame_limit == 0 );

    /* Applying it again is not a change any more. */
    before = settings;
    assert( !launcher_preset_apply( &settings, LAUNCHER_PRESET_PERFORMANCE, LAUNCHER_TRANSLATOR_ANY ) );
    assert( !memcmp( &before, &settings, sizeof(settings) ) );

    /* Nothing of it is a Custom bundle any more, or any other one. */
    assert( launcher_preset_match( &settings, LAUNCHER_TRANSLATOR_ANY ) == LAUNCHER_PRESET_PERFORMANCE );
}

static void test_each_bundle(void)
{
    struct launcher_settings settings;

    launcher_preset_defaults( &settings );
    assert( settings.fex == -1 && settings.four_cores == 1 && settings.lazy_file_views == 0 );
    assert( !settings.fex_disk_cache );
    assert( settings.fast_sync == 0 && settings.vsync == 1 && settings.frame_limit == 0 );
    assert( launcher_preset_match( &settings, LAUNCHER_TRANSLATOR_ANY ) == LAUNCHER_PRESET_BALANCED );
    assert( !launcher_preset_apply( &settings, LAUNCHER_PRESET_BALANCED, LAUNCHER_TRANSLATOR_ANY ) );

    /* Battery is Performance with a 30 fps cap and vsync on. */
    assert( launcher_preset_apply( &settings, LAUNCHER_PRESET_BATTERY, LAUNCHER_TRANSLATOR_ANY ) );
    assert( settings.fex == 2 && settings.four_cores == 1 && settings.lazy_file_views == 1 );
    assert( settings.fex_disk_cache == 0 );
    assert( settings.fast_sync == 1 && settings.vsync == 1 && settings.frame_limit == 1 );
    assert( !strcmp( launcher_frame_limit_labels[settings.frame_limit], "30" ) );
    assert( launcher_preset_match( &settings, LAUNCHER_TRANSLATOR_ANY ) == LAUNCHER_PRESET_BATTERY );

    /* Compatibility gives the fast paths up and leaves the translator alone. */
    launcher_preset_defaults( &settings );
    assert( launcher_preset_apply( &settings, LAUNCHER_PRESET_COMPATIBILITY, LAUNCHER_TRANSLATOR_ANY ) );
    assert( settings.fex == -1 && settings.four_cores == 0 && settings.lazy_file_views == 0 );
    assert( !settings.fex_disk_cache );
    assert( settings.fast_sync == 0 && settings.vsync == 1 && settings.frame_limit == 0 );
    assert( launcher_preset_match( &settings, LAUNCHER_TRANSLATOR_ANY ) == LAUNCHER_PRESET_COMPATIBILITY );

    /* A setting of the game's own makes it Custom. */
    settings.vsync = 0;
    assert( launcher_preset_match( &settings, LAUNCHER_TRANSLATOR_ANY ) == LAUNCHER_PRESET_CUSTOM );
}

static void test_translator_fallback(void)
{
    struct launcher_settings settings;

    /* Only Box64: the bundles still work, with Box64 as the translator. */
    launcher_preset_defaults( &settings );
    assert( launcher_preset_apply( &settings, LAUNCHER_PRESET_PERFORMANCE, LAUNCHER_TRANSLATOR_BOX64 ) );
    assert( settings.fex == 0 );
    assert( launcher_preset_match( &settings, LAUNCHER_TRANSLATOR_BOX64 ) == LAUNCHER_PRESET_PERFORMANCE );

    /* Stock FEX modules but not the tuned ones: FEX, not the tuned build. */
    launcher_preset_defaults( &settings );
    assert( launcher_preset_apply( &settings, LAUNCHER_PRESET_PERFORMANCE,
                                   LAUNCHER_TRANSLATOR_BOX64 | LAUNCHER_TRANSLATOR_FEX ) );
    assert( settings.fex == 1 );
    assert( launcher_preset_match( &settings, LAUNCHER_TRANSLATOR_BOX64 | LAUNCHER_TRANSLATOR_FEX ) ==
            LAUNCHER_PRESET_PERFORMANCE );

    /* Tuned modules installed later: the same settings are now that bundle
     * with the tuned translator, which is what the next run picks up. */
    assert( launcher_preset_match( &settings, LAUNCHER_TRANSLATOR_ANY ) == LAUNCHER_PRESET_CUSTOM );
    assert( launcher_preset_apply( &settings, LAUNCHER_PRESET_PERFORMANCE, LAUNCHER_TRANSLATOR_ANY ) );
    assert( settings.fex == 2 );

    /* Nothing to choose (not an x86 program, or a build without FEX): the
     * translator is left as the game had it. */
    launcher_preset_defaults( &settings );
    settings.fex = 1;
    assert( launcher_preset_apply( &settings, LAUNCHER_PRESET_PERFORMANCE, 0 ) );
    assert( settings.fex == 1 );
    assert( launcher_preset_match( &settings, 0 ) == LAUNCHER_PRESET_PERFORMANCE );

    /* An unknown bundle changes nothing. */
    launcher_preset_defaults( &settings );
    assert( !launcher_preset_apply( &settings, LAUNCHER_PRESET_CUSTOM, LAUNCHER_TRANSLATOR_ANY ) );
    assert( !launcher_preset_apply( &settings, LAUNCHER_PRESET_COUNT, LAUNCHER_TRANSLATOR_ANY ) );
}

static void test_names(void)
{
    int preset;

    for (preset = 0; preset < LAUNCHER_PRESET_COUNT; preset++)
    {
        assert( launcher_preset_label( preset ) && launcher_preset_label( preset )[0] );
        assert( launcher_preset_help( preset ) && launcher_preset_help( preset )[0] );
    }
    assert( !strcmp( launcher_preset_label( LAUNCHER_PRESET_CUSTOM ), "Custom" ) );
    assert( launcher_preset_help( LAUNCHER_PRESET_CUSTOM )[0] );
    assert( !strcmp( launcher_preset_label( LAUNCHER_PRESET_BALANCED ), "Balanced" ) );
    assert( !strcmp( launcher_preset_label( LAUNCHER_PRESET_PERFORMANCE ), "Performance" ) );
    assert( !strcmp( launcher_preset_label( LAUNCHER_PRESET_BATTERY ), "Battery" ) );
    assert( !strcmp( launcher_preset_label( LAUNCHER_PRESET_COMPATIBILITY ), "Compatibility" ) );
}

int main(void)
{
    test_defaults_match_an_empty_file();
    test_apply_leaves_what_it_does_not_own();
    test_each_bundle();
    test_translator_fallback();
    test_names();
    puts( "launcher presets: defaults, bundles, translator fallback and matching passed" );
    return 0;
}
