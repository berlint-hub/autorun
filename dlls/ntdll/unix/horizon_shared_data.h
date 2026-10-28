/*
 * Where KUSER_SHARED_DATA lives under the 32-bit forwarder.
 *
 * Windows keeps the page at 0x7ffe0000, and a 32-bit address space puts the
 * kernel's heap region straight across that address. Those are pages the
 * kernel gave this process for its heap, and this runtime keeps Wine's own
 * mappings out of them, so there is no page there of Wine's to hold on to: a
 * run of Dark Athena showed a page standing at the address only because the
 * allocator had put one there, and it was taken back while the loader was
 * still working -- the clock thread that writes the shared data every
 * millisecond took a translation fault on it, was parked as unhandled, and the
 * program never swapped a frame.
 *
 * What is left is the layout this header describes: the shared data gets a
 * page of its own, below 4 GB so the pointer fits in the guest's ntdll, and
 * 0x7ffe0000 is kept a hole that nothing is mapped at. A program that reads
 * the Windows address directly -- the bytes are not there -- has the read
 * served from the real page by horizon_redirect_user_shared_data in
 * horizon.c, which is why the moved page must not itself sit at the address.
 */

#ifndef __WINE_HORIZON_SHARED_DATA_H
#define __WINE_HORIZON_SHARED_DATA_H

#define HORIZON_SHARED_DATA_ADDRESS 0x7ffe0000ULL
#define HORIZON_SHARED_DATA_SIZE    0x1000ULL

enum horizon_shared_data_place
{
    HORIZON_SHARED_DATA_NOWHERE = 0,  /* no page anywhere: the program cannot start */
    HORIZON_SHARED_DATA_AT_FIXED,     /* the Windows address is a page of ours */
    HORIZON_SHARED_DATA_MOVED,        /* the Windows address is a hole, served by the handler */
};

/* Which of the two layouts the mapping attempts ended in. A page at the
 * Windows address wins when there is one: it needs no redirection of reads. */
static inline enum horizon_shared_data_place
horizon_shared_data_place_for( int fixed_page, int moved_page )
{
    if (fixed_page) return HORIZON_SHARED_DATA_AT_FIXED;
    if (moved_page) return HORIZON_SHARED_DATA_MOVED;
    return HORIZON_SHARED_DATA_NOWHERE;
}

/* Whether a page found for the shared data can be used: one page of it, page
 * aligned, inside what a 32-bit program can hold a pointer to -- the pointer
 * is copied into the guest's ntdll -- and not on the Windows address itself,
 * where the fault handler would never see the reads it is there to serve. */
static inline int
horizon_shared_data_move_usable( unsigned long long address, unsigned long long size,
                                 unsigned long long guest_limit )
{
    if (size != HORIZON_SHARED_DATA_SIZE) return 0;
    if (!address || (address & (HORIZON_SHARED_DATA_SIZE - 1))) return 0;
    if (address == HORIZON_SHARED_DATA_ADDRESS) return 0;
    if (address >= guest_limit || size > guest_limit - address) return 0;
    return 1;
}

#endif /* __WINE_HORIZON_SHARED_DATA_H */
