/* Host test for the launcher's settings files (source/launcher_settings.h). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../source/launcher_settings.h"

static void load_text( struct launcher_kv *kv, const char *text )
{
    kv->size = strlen( text );
    memcpy( kv->text, text, kv->size + 1 );
}

static void test_get(void)
{
    struct launcher_kv kv;
    char value[16];

    load_text( &kv, "# comment=1\r\n  Theme = glow \r\ncolumns=6\nbad=6x\nempty=\ntitle=a=b" );
    assert( launcher_kv_get( &kv, "theme", value, sizeof(value) ) && !strcmp( value, "glow" ) );
    assert( launcher_kv_get_int( &kv, "columns", 5 ) == 6 );
    assert( launcher_kv_get_int( &kv, "bad", 5 ) == 5 && launcher_kv_get_int( &kv, "empty", 5 ) == 5 );
    assert( launcher_kv_get_int( &kv, "rows", 2 ) == 2 );
    assert( !launcher_kv_get( &kv, "comment", value, sizeof(value) ) );  /* "# comment" is not the key */
    assert( launcher_kv_get( &kv, "title", value, sizeof(value) ) && !strcmp( value, "a=b" ) );  /* no newline at the end */
    assert( launcher_kv_get( &kv, "title", value, 2 ) && !strcmp( value, "a" ) );
    assert( !launcher_kv_get( &kv, "them", value, sizeof(value) ) && !launcher_kv_get( &kv, "theme2", value, sizeof(value) ) );
}

static void test_set(void)
{
    struct launcher_kv kv;
    char big[1100];

    load_text( &kv, "# keep me\nverbose=1\r\nfuture=x" );
    assert( launcher_kv_set( &kv, "verbose", "0" ) );
    assert( !strcmp( kv.text, "# keep me\nverbose=0\nfuture=x" ) );
    assert( launcher_kv_set( &kv, "profile", "1" ) );
    assert( !strcmp( kv.text, "# keep me\nverbose=0\nfuture=x\nprofile=1\n" ) );
    assert( launcher_kv_set( &kv, "verbose", NULL ) );
    assert( !strcmp( kv.text, "# keep me\nfuture=x\nprofile=1\n" ) );
    assert( launcher_kv_set( &kv, "missing", NULL ) && kv.size == strlen( "# keep me\nfuture=x\nprofile=1\n" ) );
    assert( launcher_kv_set( &kv, "profile", NULL ) && !strcmp( kv.text, "# keep me\nfuture=x\n" ) );
    assert( !launcher_kv_set( &kv, "title", "two\nlines" ) );
    memset( big, 'a', sizeof(big) - 1 );
    big[sizeof(big) - 1] = 0;
    assert( !launcher_kv_set( &kv, "title", big ) );
    assert( !strcmp( kv.text, "# keep me\nfuture=x\n" ) );

    /* The file never grows past its buffer. */
    load_text( &kv, "" );
    {
        char key[128];
        int i = 0;

        do snprintf( key, sizeof(key), "%.90s%d", big, i++ );
        while (launcher_kv_set( &kv, key, "v" ));
        assert( i > 50 && kv.size < LAUNCHER_KV_MAX && strlen( kv.text ) == kv.size );
        assert( launcher_kv_set( &kv, key, NULL ) );  /* removing still works when full */
    }
}

static void test_settings( const char *dir )
{
    struct launcher_settings settings, back;
    struct launcher_kv kv;
    char path[768], other[768], config[512];

    load_text( &kv, "" );
    launcher_settings_read( &kv, &settings );
    assert( !settings.fast_sync && settings.fex && settings.four_cores &&
            settings.d3d == LAUNCHER_D3D_DXVK && !settings.resolution );
    load_text( &kv, "four-cores=0\n" );
    launcher_settings_read( &kv, &settings );
    assert( !settings.four_cores && launcher_settings_write( &kv, &settings ) );
    launcher_settings_read( &kv, &back );
    assert( !back.four_cores );
    settings.four_cores = 1;
    assert( launcher_settings_write( &kv, &settings ) && !strstr( kv.text, "four-cores=" ) );
    load_text( &kv, "four-cores=unknown\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.four_cores );
    load_text( &kv, "cpu=box64\n" );
    launcher_settings_read( &kv, &settings );
    assert( !settings.fex );
    assert( launcher_settings_write( &kv, &settings ) );
    launcher_settings_read( &kv, &back );
    assert( !back.fex );
    /* FEX is written down when it is picked by hand: an empty key means the
     * library's own choice, fex-variant.txt's, which is -1. */
    settings.fex = 1;
    assert( launcher_settings_write( &kv, &settings ) && strstr( kv.text, "cpu=fex" ) );
    settings.fex = -1;
    assert( launcher_settings_write( &kv, &settings ) && !strstr( kv.text, "cpu=" ) );
    load_text( &kv, "cpu=unknown\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.fex );
    load_text( &kv, "sync=horizon\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.fast_sync );
    assert( launcher_settings_write( &kv, &settings ) );
    launcher_settings_read( &kv, &back );
    assert( back.fast_sync );
    settings.fast_sync = 0;
    assert( launcher_settings_write( &kv, &settings ) && !strstr( kv.text, "sync=" ) );
    load_text( &kv, "sync=fsync\n" );
    launcher_settings_read( &kv, &settings );
    assert( !settings.fast_sync );
    /* The translator's code cache is off unless a program asks for it, and a
     * card that has never had the key written reads as off. */
    load_text( &kv, "" );
    launcher_settings_read( &kv, &settings );
    assert( !settings.fex_disk_cache );
    load_text( &kv, "fex-cache=1\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.fex_disk_cache && launcher_settings_write( &kv, &settings ) &&
            strstr( kv.text, "fex-cache=1" ) );
    launcher_settings_read( &kv, &back );
    assert( back.fex_disk_cache );
    settings.fex_disk_cache = 0;
    assert( launcher_settings_write( &kv, &settings ) && !strstr( kv.text, "fex-cache=" ) );
    load_text( &kv, "fex-cache=0\n" );
    launcher_settings_read( &kv, &settings );
    assert( !settings.fex_disk_cache );
    /* Mesa's two OpenGL switches: off unless a program turns them on, and a
     * key written as 0 or "off" is off. The year is a choice, and the switch
     * it grew out of (gl-2003) still reads as its own. */
    load_text( &kv, "" );
    launcher_settings_read( &kv, &settings );
    assert( !settings.gl_thread && !settings.gl_year );
    load_text( &kv, "gl-thread=1\ngl-year=2009\ngl-driver=zink\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.gl_thread && settings.gl_year == 3 && settings.gl_driver == 1 );
    assert( launcher_gl_years[settings.gl_year] == 2009 );
    assert( !strcmp( launcher_gl_driver_values[settings.gl_driver], "zink" ) );
    assert( launcher_settings_write( &kv, &settings ) && strstr( kv.text, "gl-thread=1" ) &&
            strstr( kv.text, "gl-year=2009" ) && strstr( kv.text, "gl-driver=zink" ) );
    launcher_settings_read( &kv, &back );
    assert( back.gl_thread && back.gl_year == 3 && back.gl_driver == 1 );
    /* NVC0 stays the default for old settings files, and unknown Mesa driver
     * names fail closed to that default instead of reaching the environment. */
    load_text( &kv, "gl-driver=unknown\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.gl_driver == 0 );
    /* The switch of the build before this one, which knew one year. */
    load_text( &kv, "gl-2003=on\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.gl_year == 1 && launcher_gl_years[settings.gl_year] == 2003 );
    /* Written back, it is the year key: the old one is gone. */
    assert( launcher_settings_write( &kv, &settings ) && strstr( kv.text, "gl-year=2003" ) &&
            !strstr( kv.text, "gl-2003=" ) );
    /* A year no driver is offered is off, as is a number that is not one. */
    load_text( &kv, "gl-year=2015\n" );
    launcher_settings_read( &kv, &settings );
    assert( !settings.gl_year );
    load_text( &kv, "gl-year=later\n" );
    launcher_settings_read( &kv, &settings );
    assert( !settings.gl_year );
    load_text( &kv, "gl-year=2006\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.gl_year == 2 && launcher_gl_years[settings.gl_year] == 2006 );
    settings.gl_thread = 0;
    settings.gl_year = 0;
    settings.gl_driver = 0;
    assert( launcher_settings_write( &kv, &settings ) && !strstr( kv.text, "gl-thread=" ) &&
            !strstr( kv.text, "gl-year=" ) && !strstr( kv.text, "gl-driver=" ) );
    load_text( &kv, "resolution=1920x1080\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.resolution == 1 );
    assert( launcher_settings_write( &kv, &settings ) );
    launcher_settings_read( &kv, &back );
    assert( back.resolution == 1 );
    settings.resolution = 0;
    assert( launcher_settings_write( &kv, &settings ) && !strstr( kv.text, "resolution=" ) );
    load_text( &kv, "resolution=unknown\n" );
    launcher_settings_read( &kv, &settings );
    assert( !settings.resolution );

    load_text( &kv, "voodoo=1\nvoodoo-api=d3d11_fl10_1\nvoodoo-scaling=stretched\n"
                    "voodoo-resolution=1024x768\nvoodoo-msaa=4x\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.voodoo && settings.voodoo_api == 2 && settings.voodoo_scaling == 1 &&
            settings.voodoo_resolution == 3 && settings.voodoo_msaa == 3 );
    assert( launcher_settings_write( &kv, &settings ) );
    launcher_settings_read( &kv, &back );
    assert( back.voodoo && back.voodoo_api == 2 && back.voodoo_scaling == 1 &&
            back.voodoo_resolution == 3 && back.voodoo_msaa == 3 );
    settings.voodoo = 0;
    assert( launcher_settings_write( &kv, &settings ) && !strstr( kv.text, "voodoo=" ) );
    settings.voodoo = 1;
    assert( launcher_voodoo_config( &settings, config, sizeof(config) ) );
    assert( strstr( config, "Version = 0x280\n" ) && strstr( config, "[General]\n" ) &&
            strstr( config, "OutputAPI = d3d11_fl10_1\n" ) && strstr( config, "ScalingMode = stretched\n" ) &&
            strstr( config, "[DirectX]\n" ) && strstr( config, "Resolution = 1024x768\n" ) &&
            strstr( config, "Antialiasing = 4x\n" ) && strstr( config, "VRAM = 1024\n" ) &&
            strstr( config, "dgVoodooWatermark = false\n" ) );
    settings.voodoo_api = LAUNCHER_VOODOO_API_COUNT;
    assert( !launcher_voodoo_config( &settings, config, sizeof(config) ) );
    settings.voodoo_api = 0;
    assert( launcher_settings_write( &kv, &settings ) && strstr( kv.text, "voodoo=1\n" ) );

    assert( launcher_settings_path( "sdmc:/switch/wine/drive_c/nfsu2/SPEED2.EXE", path, sizeof(path) ) );
    assert( !strcmp( path, "sdmc:/switch/wine/drive_c/nfsu2/SPEED2.wine-nx.txt" ) );
    assert( !launcher_settings_path( "sdmc:/readme.txt", path, sizeof(path) ) );
    assert( !launcher_settings_path( "sdmc:/a.exe", path, 19 ) && launcher_settings_path( "sdmc:/a.exe", path, 20 ) );

    load_text( &kv, "verbose=on\nprofile=off\nwindows=framebuffer\nd3d9=DXVK\nhidden=yes\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.verbose == 1 && settings.profile == 0 && settings.framebuffer == 1 &&
            settings.d3d == LAUNCHER_D3D_DXVK_VKD3D );
    assert( settings.hidden == 0 && !settings.title[0] );  /* only 1 or on hides */

    load_text( &kv, "d3d=wine\nd3d9=dxvk\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.d3d == LAUNCHER_D3D_WINE );
    load_text( &kv, "d3d=DXVK\ndxvk-version=2.7.1\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.d3d == LAUNCHER_D3D_DXVK_VKD3D && settings.dxvk_source == DXVK_SOURCE_OFFICIAL &&
            !strcmp( settings.dxvk_version, "2.7.1" ) );
    load_text( &kv, "d3d=dxvk\ndxvk-source=gplasync\ndxvk-version=3.1.1-1\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.dxvk_source == DXVK_SOURCE_GPLASYNC && !strcmp( settings.dxvk_version, "3.1.1-1" ) );
    assert( launcher_settings_write( &kv, &settings ) && strstr( kv.text, "dxvk-source=gplasync\n" ) );
    load_text( &kv, "dxvk-source=sarek\ndxvk-version=1.13.0\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.d3d == LAUNCHER_D3D_DXVK && settings.dxvk_source == DXVK_SOURCE_SAREK &&
            !strcmp( settings.dxvk_version, "1.13.0" ) );
    load_text( &kv, "d3d=dxvk+vkd3d\ndxvk-source=sarek\ndxvk-version=1.13.0\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.d3d == LAUNCHER_D3D_DXVK_VKD3D && settings.dxvk_source == DXVK_SOURCE_OFFICIAL &&
            !settings.dxvk_version[0] );
    load_text( &kv, "d3d=DXVK\ndxvk-version=../../bad\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.d3d == LAUNCHER_D3D_DXVK_VKD3D && !settings.dxvk_version[0] );
    assert( launcher_dxvk_version_valid( "3.1.1" ) && launcher_dxvk_version_valid( "2.0-rc1" ) );
    assert( !launcher_dxvk_version_valid( "" ) && !launcher_dxvk_version_valid( "../3.1" ) &&
            !launcher_dxvk_version_valid( "3.1/other" ) );
    assert( launcher_dxvk_version_selectable( "1.0" ) && launcher_dxvk_version_selectable( "3.1.1" ) );
    assert( !launcher_dxvk_version_selectable( "0.96" ) && !launcher_dxvk_version_selectable( "bad" ) );
    assert( !launcher_dxvk_vkd3d_compatible( DXVK_SOURCE_OFFICIAL, "2.0" ) &&
            launcher_dxvk_vkd3d_compatible( DXVK_SOURCE_OFFICIAL, "2.1" ) &&
            launcher_dxvk_vkd3d_compatible( DXVK_SOURCE_GPLASYNC, "3.0-1" ) &&
            !launcher_dxvk_vkd3d_compatible( DXVK_SOURCE_SAREK, "3.0" ) );
    assert( !strcmp( launcher_dxvk_directory( 0x014c, DXVK_SOURCE_OFFICIAL ), "dxvk" ) );
    assert( !strcmp( launcher_dxvk_directory( 0x8664, DXVK_SOURCE_OFFICIAL ), "dxvk64" ) );
    assert( !strcmp( launcher_dxvk_directory( 0x014c, DXVK_SOURCE_SAREK ), "dxvk-sarek" ) );
    assert( !strcmp( launcher_dxvk_directory( 0x8664, DXVK_SOURCE_GPLASYNC ), "dxvk-gplasync64" ) );
    assert( !launcher_dxvk_directory( 0xaa64, DXVK_SOURCE_OFFICIAL ) &&
            !launcher_dxvk_directory( 0, DXVK_SOURCE_OFFICIAL ) );
    assert( LAUNCHER_HUD_COUNT == 4 );
    assert( !strcmp( launcher_hud_values[2], "api,fps,frametimes" ) );
    assert( !strcmp( launcher_hud_values[3],
                     "version,api,devinfo,fps,memory,frametimes,compiler" ) );
    load_text( &kv, "dxvk-hud=api,fps,frametimes\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.dxvk_hud == 2 );
    load_text( &kv, "dxvk-hud=version,api,devinfo,fps,memory,frametimes,compiler\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.dxvk_hud == 3 );
    settings.dxvk_hud = LAUNCHER_HUD_COUNT;
    assert( !launcher_settings_write( &kv, &settings ) );
    settings.dxvk_hud = 0;
    assert( LAUNCHER_FRAME_LIMIT_COUNT == 8 );
    assert( launcher_dxvk_config( &settings, config, sizeof(config) ) );
    /* A game's own dxvk.conf follows, so its lines win; one without a final
     * newline gets one, and one that does not fit is refused whole. */
    {
        char with_game[256];
        static const char game[] = "d3d9.maxAvailableMemory = 512";

        assert( launcher_dxvk_config( &settings, with_game, sizeof(with_game) ) );
        assert( launcher_dxvk_config_add( with_game, sizeof(with_game), game, sizeof(game) - 1 ) );
        assert( strstr( with_game, "d3d9.presentInterval" ) < strstr( with_game, "d3d9.maxAvailableMemory = 512\n" ) );
        assert( with_game[strlen( with_game ) - 1] == '\n' );
        assert( launcher_dxvk_config_add( with_game, sizeof(with_game), "", 0 ) );
        assert( !launcher_dxvk_config_add( with_game, strlen( with_game ) + 8, game, sizeof(game) - 1 ) );
    }
    assert( strstr( config, "dxgi.syncInterval = 1" ) );
    assert( !strstr( config, "dxvk.enableDescriptorBuffer" ) );
    settings.dxvk_hud = 1;
    assert( launcher_dxvk_config( &settings, config, sizeof(config) ) );
    assert( strstr( config, "dxvk.enableDescriptorBuffer = False" ) );
    settings.dxvk_hud = 0;
    assert( launcher_settings_write( &kv, &settings ) && !strstr( kv.text, "frame-limit=" ) );
    assert( launcher_dxvk_version_directory( 0x8664, DXVK_SOURCE_OFFICIAL, "2.7.1", path, sizeof(path) ) &&
            !strcmp( path, "dxvk64\\versions\\2.7.1" ) );
    assert( launcher_dxvk_version_directory( 0x014c, DXVK_SOURCE_OFFICIAL, "", path, sizeof(path) ) &&
            !strcmp( path, "dxvk" ) );
    assert( launcher_dxvk_version_directory( 0x8664, DXVK_SOURCE_GPLASYNC, "3.1.1-1", path, sizeof(path) ) &&
            !strcmp( path, "dxvk-gplasync64\\versions\\3.1.1-1" ) );
    assert( !launcher_dxvk_version_directory( 0x8664, DXVK_SOURCE_OFFICIAL, "../bad", path, sizeof(path) ) );

    load_text( &kv, "upscaling=fsr\nupscaling-sharpness=80%\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.upscaling == 1 && settings.upscaling_sharpness == 4 );

    load_text( &kv, "upscaling=integer\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.upscaling == 2 && settings.upscaling_sharpness == 2 );

    settings.upscaling = 1;
    settings.upscaling_sharpness = 3;
    assert( launcher_settings_write( &kv, &settings ) );
    assert( strstr( kv.text, "upscaling=fsr" ) && strstr( kv.text, "upscaling-sharpness=60%" ) );

    settings.upscaling = 0;
    assert( launcher_settings_write( &kv, &settings ) );
    assert( !strstr( kv.text, "upscaling=" ) && !strstr( kv.text, "upscaling-sharpness=" ) );

    /* The Scaling setting, by its own key; the old two-way "aspect" one still
     * reads as the widescreen hack it was. */
    load_text( &kv, "scaling=integer\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.scaling == SCREEN_SCALING_INTEGER );
    load_text( &kv, "scaling=full\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.scaling == SCREEN_SCALING_FULL );
    load_text( &kv, "aspect=stretch\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.scaling == SCREEN_SCALING_STRETCH_43 );
    load_text( &kv, "" );
    launcher_settings_read( &kv, &settings );
    assert( settings.scaling == SCREEN_SCALING_ASPECT );
    settings.scaling = SCREEN_SCALING_STRETCH_43;
    assert( launcher_settings_write( &kv, &settings ) );
    assert( strstr( kv.text, "scaling=stretch_4_3" ) && !strstr( kv.text, "aspect=" ) );
    settings.scaling = SCREEN_SCALING_ASPECT;
    assert( launcher_settings_write( &kv, &settings ) );
    assert( !strstr( kv.text, "scaling=" ) );

    snprintf( path, sizeof(path), "%s/game.wine-nx.txt", dir );
    load_text( &kv, "# written by hand\n" );
    memset( &settings, 0, sizeof(settings) );
    settings.fex = -1;
    settings.four_cores = 1;
    settings.vsync = settings.lsfg_performance = settings.lsfg_flow = 1;
    strcpy( settings.title, "Need for Speed" );
    settings.hidden = 1;
    settings.verbose = -1;
    settings.profile = 1;
    settings.framebuffer = 0;
    settings.scaling = SCREEN_SCALING_ASPECT;
    settings.d3d = LAUNCHER_D3D_DXVK;
    strcpy( settings.dxvk_version, "2.7.1" );
    assert( launcher_settings_write( &kv, &settings ) && launcher_kv_save( &kv, path ) );
    assert( launcher_kv_load( &kv, path ) );
    assert( !strcmp( kv.text, "# written by hand\ntitle=Need for Speed\nhidden=1\nprofile=1\nwindows=compositor\ndxvk-version=2.7.1\n" ) );
    launcher_settings_read( &kv, &back );
    assert( !strcmp( back.title, settings.title ) && back.hidden == 1 && back.verbose == -1 && back.profile == 1 );
    assert( back.framebuffer == 0 && back.d3d == LAUNCHER_D3D_DXVK && !strcmp( back.dxvk_version, "2.7.1" ) );

    /* Back to the global settings: only the comment stays; without it the file goes. */
    memset( &settings, 0, sizeof(settings) );
    settings.fex = -1;
    settings.four_cores = 1;
    settings.d3d = LAUNCHER_D3D_DXVK;
    settings.scaling = SCREEN_SCALING_ASPECT;
    settings.verbose = settings.profile = settings.framebuffer = -1;
    settings.vsync = settings.lsfg_performance = settings.lsfg_flow = 1;
    assert( launcher_settings_write( &kv, &settings ) && !strcmp( kv.text, "# written by hand\n" ) );
    assert( launcher_kv_save( &kv, path ) && !access( path, F_OK ) );
    load_text( &kv, "" );
    assert( launcher_kv_save( &kv, path ) && access( path, F_OK ) );
    assert( launcher_kv_save( &kv, path ) );  /* already gone */
    snprintf( path, sizeof(path), "%s/game.wine-nx.txt.new", dir );
    assert( access( path, F_OK ) );

    snprintf( path, sizeof(path), "%s/missing.txt", dir );
    assert( launcher_kv_load( &kv, path ) && kv.size == 0 );

    assert( launcher_settings_on_usb( "ums0:/Games/Test/Game.EXE" ) );
    assert( !launcher_settings_on_usb( "sdmc:/switch/wine/drive_c/Game.EXE" ) );
    assert( launcher_program_settings_path( dir, "ums0:/Games/Test/Game.EXE", path, sizeof(path) ) );
    assert( launcher_program_settings_path( dir, "UMS0:\\games\\test\\game.exe", other, sizeof(other) ) );
    assert( !strcmp( path, other ) );
    assert( strstr( path, "/program-settings/" ) );
    assert( launcher_program_settings_path( dir, "sdmc:/Games/Test/Game.EXE", path, sizeof(path) ) );
    assert( !strcmp( path, "sdmc:/Games/Test/Game.wine-nx.txt" ) );
}

/* A settings file is written through tables and states: a field outside what
 * they hold is refused rather than read past the end of one. */
static void test_write_refuses_what_it_cannot_say(void)
{
    struct launcher_kv kv;
    struct launcher_settings settings;

    memset( &kv, 0, sizeof(kv) );
    memset( &settings, 0, sizeof(settings) );   /* every field at the first of its table */
    assert( launcher_settings_write( &kv, &settings ) );

    settings.verbose = 2;
    assert( !launcher_settings_write( &kv, &settings ) );
    settings.verbose = -1;

    settings.framebuffer = 3;
    assert( !launcher_settings_write( &kv, &settings ) );
    settings.framebuffer = -1;

    settings.fex = 3;
    assert( !launcher_settings_write( &kv, &settings ) );
    settings.fex = -1;

    settings.frame_limit = LAUNCHER_FRAME_LIMIT_COUNT;
    assert( !launcher_settings_write( &kv, &settings ) );
    settings.frame_limit = 0;

    settings.d3d = LAUNCHER_D3D_DXVK_VKD3D + 1;
    assert( !launcher_settings_write( &kv, &settings ) );
    settings.d3d = LAUNCHER_D3D_DXVK;

    assert( launcher_settings_write( &kv, &settings ) );
}

/* A game that runs the same as another takes how the other runs, but keeps its
 * own name and whether it is hidden. */
static void test_copy(void)
{
    struct launcher_settings from, to;

    memset( &from, 0, sizeof(from) );
    memset( &to, 0, sizeof(to) );
    snprintf( from.title, sizeof(from.title), "SPEED2" );
    from.hidden = 1;
    from.fex = 2;
    from.d3d = LAUNCHER_D3D_DXVK_VKD3D;
    from.frame_limit = 3;
    from.vsync = 0;
    from.upscaling = 1;
    from.lsfg_enabled = 1;
    snprintf( from.dxvk_version, sizeof(from.dxvk_version), "2.4.1" );
    snprintf( to.title, sizeof(to.title), "Mine" );
    to.hidden = 0;
    to.fex = 0;

    launcher_settings_copy( &to, &from );
    assert( !strcmp( to.title, "Mine" ) && to.hidden == 0 );   /* the game's own */
    assert( to.fex == 2 && to.d3d == LAUNCHER_D3D_DXVK_VKD3D && to.frame_limit == 3 && !to.vsync );
    assert( to.upscaling == 1 && to.lsfg_enabled == 1 && !strcmp( to.dxvk_version, "2.4.1" ) );
    /* The game it came from is left as it was. */
    assert( !strcmp( from.title, "SPEED2" ) && from.hidden == 1 );

    /* A game that has no title of its own keeps having none. */
    to.title[0] = 0;
    launcher_settings_copy( &to, &from );
    assert( !to.title[0] );
}

/* A game whose panel follows its own frame rate: the switch is per game and
 * defaults to off, it survives the settings file, and while it is on the DXVK
 * profile carries no cap of its own to fight the panel with. */
static void test_panel_follow(void)
{
    struct launcher_kv kv;
    struct launcher_settings settings;
    char config[512];

    load_text( &kv, "frame-limit-sync=1\nframe-limit=60\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.frame_limit_sync == 1 && settings.frame_limit == 4 );
    assert( launcher_settings_write( &kv, &settings ) && strstr( kv.text, "frame-limit-sync=1" ) );

    load_text( &kv, "frame-limit-sync=on\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.frame_limit_sync == 1 );
    load_text( &kv, "frame-limit-sync=off\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.frame_limit_sync == 0 );
    /* Anything else is off: a switch nobody can read is not a cap anyone keeps. */
    load_text( &kv, "frame-limit-sync=yes please\n" );
    launcher_settings_read( &kv, &settings );
    assert( settings.frame_limit_sync == 0 );

    /* Absent means off, and off is not written out. */
    load_text( &kv, "" );
    launcher_settings_read( &kv, &settings );
    assert( settings.frame_limit_sync == 0 );
    settings.frame_limit = 4;
    assert( launcher_settings_write( &kv, &settings ) );
    assert( strstr( kv.text, "frame-limit=60" ) && !strstr( kv.text, "frame-limit-sync" ) );

    /* With the switch on, a limit set in the same file is kept in the file but
     * not handed to DXVK: the panel is the limiter then. */
    settings.frame_limit_sync = 1;
    assert( launcher_dxvk_config( &settings, config, sizeof(config) ) );
    assert( !strstr( config, "maxFrameRate" ) );
    settings.frame_limit_sync = 0;
    assert( launcher_dxvk_config( &settings, config, sizeof(config) ) );
    assert( strstr( config, "dxvk.maxFrameRate = 60" ) );

    /* A game that switches it on keeps the switch across a copy into a preset. */
    settings.frame_limit_sync = 1;
    {
        struct launcher_settings into;

        memset( &into, 0, sizeof(into) );
        launcher_settings_copy( &into, &settings );
        assert( into.frame_limit_sync == 1 );
    }
}

int main(void)
{
    char dir[] = "/tmp/launcher-settings.XXXXXX", command[64];

    assert( mkdtemp( dir ) );
    test_get();
    test_set();
    test_write_refuses_what_it_cannot_say();
    test_copy();
    test_panel_follow();
    test_settings( dir );
    snprintf( command, sizeof(command), "rm -rf '%s'", dir );
    /* A test directory left behind is not a failure of what was tested, but the
     * result is read: with fortify on, ignoring it is a build error. */
    if (system( command )) printf( "launcher_settings: could not remove %s\n", dir );
    printf( "launcher_settings: all tests passed\n" );
    return 0;
}
