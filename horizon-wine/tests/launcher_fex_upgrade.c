/* A program set to the newest of Carafe's FEX builds starts on the newest
 * release (launcher_graphics_ensure_fex): a newer one in the release list is
 * installed first, a version chosen by hand is kept, and a list that does not
 * come within the limit leaves the program on what is installed. The release
 * functions and the screens are stand-ins; the catalog worker is a real
 * thread, as on a card. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "launcher_graphics.h"
#include "launcher_ui.h"
#include "dxvk_releases.h"

static const char *installed[8];
static const char *cached[8], *fresh[8];
static unsigned int fresh_delay_ms;
static int installs, messages, fresh_requests;
static char last_install[32];

static void set_list( const char **list, const char *const *versions )
{
    int i;

    memset( list, 0, 8 * sizeof(*list) );
    for (i = 0; versions && versions[i]; i++) list[i] = versions[i];
}

static int newer( const char *a, const char *b )
{
    unsigned int ya, na, yb = 0, nb = 0;

    if (sscanf( a, "%u-%u", &ya, &na ) != 2) return 0;
    if (b[0] && sscanf( b, "%u-%u", &yb, &nb ) != 2) return 1;
    return ya != yb ? ya > yb : na > nb;
}

int dxvk_release_newer( enum dxvk_source source, const char *candidate, const char *installed_version )
{
    (void)source;
    return !strstr( candidate, "rc" ) && newer( candidate, installed_version );
}

enum dxvk_result dxvk_release_catalog( enum dxvk_source source, const char *runtime_dir, struct dxvk_release *releases,
                                       int max_releases, int *count, int cache_only,
                                       dxvk_progress_callback progress, void *opaque )
{
    const char **list = cache_only ? cached : fresh;
    int i;

    (void)source; (void)runtime_dir;
    *count = 0;
    if (!cache_only)
    {
        Uint32 until = SDL_GetTicks() + fresh_delay_ms;

        __atomic_add_fetch( &fresh_requests, 1, __ATOMIC_RELAXED );
        while (!SDL_TICKS_PASSED( SDL_GetTicks(), until ))
        {
            if (progress && progress( opaque, DXVK_PROGRESS_DOWNLOAD, 0, 0 )) return DXVK_CANCELLED;
            SDL_Delay( 10 );
        }
    }
    for (i = 0; list[i] && *count < max_releases; i++)
    {
        struct dxvk_release *release = releases + (*count)++;

        memset( release, 0, sizeof(*release) );
        snprintf( release->version, sizeof(release->version), "%s", list[i] );
        release->prerelease = strstr( list[i], "rc" ) != NULL;
        release->size = 1170000;
    }
    return *count ? DXVK_OK : DXVK_NOT_FOUND;
}

enum dxvk_result dxvk_install_release( enum dxvk_source source, const char *runtime_dir, const struct dxvk_release *release,
                                       dxvk_progress_callback progress, void *opaque )
{
    static char versions[8][32];
    int i;

    (void)source; (void)runtime_dir; (void)progress; (void)opaque;
    installs++;
    snprintf( last_install, sizeof(last_install), "%s", release->version );
    for (i = 0; installed[i]; i++) if (!strcmp( installed[i], release->version )) return DXVK_OK;
    snprintf( versions[i], sizeof(versions[i]), "%s", release->version );
    installed[i] = versions[i];
    return DXVK_OK;
}

void dxvk_resolve_version( enum dxvk_source source, const char *runtime_dir, unsigned short machine, const char *requested,
                           struct dxvk_version *selected )
{
    int i;

    (void)source; (void)runtime_dir; (void)machine;
    memset( selected, 0, sizeof(*selected) );
    if (requested[0])
    {
        snprintf( selected->version, sizeof(selected->version), "%s", requested );
        for (i = 0; installed[i]; i++) if (!strcmp( installed[i], requested )) selected->installed = 1;
        return;
    }
    for (i = 0; installed[i]; i++)
        if (!selected->installed || newer( installed[i], selected->version ))
        {
            snprintf( selected->version, sizeof(selected->version), "%s", installed[i] );
            selected->installed = 1;
        }
}

int dxvk_release_installed( enum dxvk_source source, const char *runtime_dir, unsigned short machine, const char *version )
{
    struct dxvk_version selected;

    dxvk_resolve_version( source, runtime_dir, machine, version, &selected );
    return version[0] && selected.installed;
}

int dxvk_root_version( enum dxvk_source source, const char *runtime_dir, unsigned short machine, char *version, size_t size )
{
    (void)source; (void)runtime_dir; (void)machine;
    if (size) version[0] = 0;
    return 0;
}

const char *dxvk_result_message( enum dxvk_result result )
{
    (void)result;
    return "stand-in";
}

enum dxvk_result vkd3d_release_catalog( const char *runtime_dir, struct dxvk_release *releases, int max_releases, int *count,
                                       int cache_only, dxvk_progress_callback progress, void *opaque )
{
    (void)runtime_dir; (void)releases; (void)max_releases; (void)cache_only; (void)progress; (void)opaque;
    *count = 0;
    return DXVK_NOT_FOUND;
}

enum dxvk_result vkd3d_install_release( const char *runtime_dir, const struct dxvk_release *release,
                                       dxvk_progress_callback progress, void *opaque )
{
    (void)runtime_dir; (void)release; (void)progress; (void)opaque;
    return DXVK_IO_ERROR;
}

int vkd3d_release_installed( const char *runtime_dir, unsigned short machine, const char *version )
{
    (void)runtime_dir; (void)machine; (void)version;
    return 0;
}

int vkd3d_root_version( const char *runtime_dir, unsigned short machine, char *version, size_t size )
{
    (void)runtime_dir; (void)machine;
    if (size) version[0] = 0;
    return 0;
}

void vkd3d_resolve_version( const char *runtime_dir, unsigned short machine, const char *requested,
                            struct dxvk_version *selected )
{
    (void)runtime_dir; (void)machine; (void)requested;
    memset( selected, 0, sizeof(*selected) );
}

void ui_progress_begin( struct ui *ui ) { (void)ui; }
void ui_progress_end( struct ui *ui ) { (void)ui; }
void ui_progress_update( struct ui *ui, const char *title, const char *status,
                         unsigned long long current, unsigned long long total )
{
    (void)ui; (void)title; (void)status; (void)current; (void)total;
}
void ui_message( struct ui *ui, const char *title, const char *message )
{
    (void)ui; (void)title; (void)message;
    messages++;
}
void ui_wait( struct ui *ui ) { (void)ui; SDL_Delay( 5 ); }
int ui_settings_dropdown( struct ui *ui, const struct ui_list *anchor, const struct ui_row *rows, int count, int selected )
{
    (void)ui; (void)anchor; (void)rows; (void)count; (void)selected;
    return -1;
}
int ui_settings_dropdown_live( struct ui *ui, const struct ui_list *anchor, const struct ui_row *rows,
                               int selection, int (*update)( void *data, int *selection ), void *data )
{
    (void)ui; (void)anchor; (void)rows; (void)selection; (void)update; (void)data;
    return -1;
}

static void start( const char *const *have, const char *const *cache, const char *const *list, unsigned int delay_ms,
                   const char *chosen, const char *expected, int expected_installs )
{
    static struct ui ui;
    struct launcher_graphics *g;
    char version[32];
    Uint32 began;

    set_list( installed, have );
    set_list( cached, cache );
    set_list( fresh, list );
    fresh_delay_ms = delay_ms;
    installs = messages = 0;
    __atomic_store_n( &fresh_requests, 0, __ATOMIC_RELAXED );
    last_install[0] = 0;
    memset( &ui, 0, sizeof(ui) );
    ui.running = 1;
    assert( (g = launcher_graphics_create( &ui, "/nowhere" )) );
    snprintf( version, sizeof(version), "%s", chosen );
    began = SDL_GetTicks();
    assert( launcher_graphics_ensure_fex( g, 2, version ) );
    assert( SDL_GetTicks() - began < 5500 );
    if (strcmp( version, expected ) || installs != expected_installs)
    {
        fprintf( stderr, "chosen '%s': started on %s after %d installs, expected %s after %d\n",
                 chosen, version, installs, expected, expected_installs );
        abort();
    }
    assert( !messages );
    /* A version chosen by hand does not ask for the list at all. */
    if (chosen[0]) assert( !__atomic_load_n( &fresh_requests, __ATOMIC_RELAXED ) );
    launcher_graphics_destroy( g );
}

int main( void )
{
    static const char *const old[] = { "2611-1", NULL }, *const fixed[] = { "2611-1", "2611-2", NULL };
    static const char *const list[] = { "2611-3-rc1", "2611-2", "2611-1", "2609-6", NULL };
    static const char *const stale[] = { "2611-1", "2609-6", NULL };

    assert( !SDL_Init( SDL_INIT_EVENTS ) );
    /* The fix is out: the card's 2611-1 gives way to 2611-2, not to the prerelease. */
    start( old, stale, list, 50, "", "2611-2", 1 );
    /* Already the newest: nothing is downloaded. */
    start( fixed, list, list, 50, "", "2611-2", 0 );
    /* Chosen by hand: kept, and the list is not asked for. */
    start( fixed, list, list, 50, "2611-1", "2611-1", 0 );
    /* The list does not come in time and the card's knows nothing newer:
     * the program starts on what is installed, after the limit. */
    start( old, stale, list, 5000, "", "2611-1", 0 );
    /* ...but what the card's list knows is enough. */
    start( old, list, list, 5000, "", "2611-2", 1 );
    /* None on the card: the newest release, as before. */
    start( NULL, stale, list, 50, "", "2611-2", 1 );
    SDL_Quit();
    printf( "launcher FEX builds: a newer release for the newest, the limit, a chosen version and none installed passed\n" );
    return 0;
}
