/* Host test for dlls/ntdll/unix/horizon_shared_data.h.
 *
 * The header decides where KUSER_SHARED_DATA went, and whether a page found
 * for it can carry the shared data. Both answers decide what happens to a
 * program that reads 0x7ffe0000 itself, so they are checked here rather than
 * on a console: the Windows address may never be the moved page, and the
 * pointer the guest gets has to fit in 32 bits.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../../dlls/ntdll/unix/horizon_shared_data.h"

#define GUEST_LIMIT 0x100000000ULL

static int failures;

static void check( int condition, const char *what )
{
    if (condition) return;
    printf( "shared_data_place: FAILED %s\n", what );
    failures++;
}

static void check_place( int fixed_page, int moved_page, enum horizon_shared_data_place expected,
                         const char *what )
{
    check( horizon_shared_data_place_for( fixed_page, moved_page ) == expected, what );
}

static void check_usable( unsigned long long address, unsigned long long size,
                          unsigned long long guest_limit, int expected, const char *what )
{
    check( !!horizon_shared_data_move_usable( address, size, guest_limit ) == expected, what );
}

int main(void)
{
    /* A page at the Windows address is used where it can be: no redirection,
     * and the program reads exactly what Windows would give it. */
    check_place( 1, 0, HORIZON_SHARED_DATA_AT_FIXED, "a page at 0x7ffe0000 is used there" );
    check_place( 1, 1, HORIZON_SHARED_DATA_AT_FIXED, "the Windows address wins over a moved page" );

    /* The 32-bit forwarder's case: no page at the address, one of our own
     * elsewhere, and the reads of the address served by the handler. */
    check_place( 0, 1, HORIZON_SHARED_DATA_MOVED, "a moved page is the fallback" );

    /* Neither: the caller has to stop instead of starting a program that would
     * fault on its first look at the clock. */
    check_place( 0, 0, HORIZON_SHARED_DATA_NOWHERE, "no page at all is nowhere" );

    /* Where a moved page may be. */
    check_usable( 0x10000000ULL, 0x1000, GUEST_LIMIT, 1, "a page below 4 GB is usable" );
    check_usable( 0x7fff0000ULL, 0x1000, GUEST_LIMIT, 1, "the last page below 2 GB is usable" );
    check_usable( 0xfffff000ULL, 0x1000, GUEST_LIMIT, 1, "the page ending at 4 GB is usable" );

    /* And where it may not be. */
    check_usable( 0x7ffe0000ULL, 0x1000, GUEST_LIMIT, 0, "the Windows address itself is refused" );
    check_usable( 0x100000000ULL, 0x1000, GUEST_LIMIT, 0, "a page above 4 GB is refused" );
    check_usable( 0x100001000ULL, 0x1000, GUEST_LIMIT, 0, "a page above 4 GB is refused, not wrapped" );
    check_usable( 0x10000010ULL, 0x1000, GUEST_LIMIT, 0, "an unaligned page is refused" );
    check_usable( 0x10000000ULL, 0x2000, GUEST_LIMIT, 0, "more than one page is refused" );
    check_usable( 0, 0x1000, GUEST_LIMIT, 0, "the null address is refused" );
    check_usable( 0xfffff800ULL, 0x1000, GUEST_LIMIT, 0, "a page straddling 4 GB is refused" );

    /* A guest whose limit is already 2 GB keeps its page below it. */
    check_usable( 0x7ffe0000ULL, 0x1000, 0x80000000ULL, 0, "the Windows address is refused below 2 GB too" );
    check_usable( 0x7ff00000ULL, 0x1000, 0x80000000ULL, 1, "a page below 2 GB is usable there" );

    if (failures)
    {
        printf( "shared_data_place: %d failed\n", failures );
        return 1;
    }
    printf( "shared_data_place: ok\n" );
    return 0;
}
