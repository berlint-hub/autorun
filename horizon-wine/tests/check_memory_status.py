#!/usr/bin/env python3
"""Run horizon_get_memory_info on made-up Horizon and allocator figures, and
derive what GlobalMemoryStatusEx would report from them.

libnx claims the whole heap from Horizon at startup, so InfoType_UsedMemorySize
stays a few megabytes below InfoType_TotalMemorySize however little the process
has allocated, and a report built from it is wrong in both directions. Wine's
ullAvailPageFile is exactly total - used: Fallout New Vegas quits with "Not
enough memory to run application." unless it is at least 512 MB, and it read
4 MB on build 109; Dark Athena read 140 MB free before it had loaded anything
and stopped reading its level after 19 MB. What a program can still get is what
the allocator can hand out, and that is what the function asks for now."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'dlls/ntdll/unix/horizon.c').read_text()

PAGE = 0x1000
MB = 1 << 20
NEEDED = 0x20000000  # what Fallout New Vegas wants of ullAvailPageFile


def block(marker):
    start = source.index(marker)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


fixture = f'''
#include <stdio.h>
#include <stdlib.h>

typedef unsigned long long u64;
#define R_SUCCEEDED(rc) ((rc) == 0)
#define CUR_PROCESS_HANDLE 0xffff8001u
enum {{ InfoType_TotalMemorySize = 6, InfoType_UsedMemorySize = 7 }};

static u64 fake_total, fake_kernel_used;
static unsigned long long fake_free;
static int fake_rc;

static int svcGetInfo( u64 *out, int id, unsigned int handle, u64 sub )
{{
    (void)handle; (void)sub;
    if (fake_rc) return fake_rc;
    *out = id == InfoType_TotalMemorySize ? fake_total : fake_kernel_used;
    return 0;
}}

/* The allocator's own books, from horizon-wine/source/native_heap.c. */
size_t wine_nx_native_heap_free_estimate( size_t *lower_bound )
{{
    if (lower_bound) *lower_bound = fake_free;
    return fake_free;
}}

{block('void horizon_get_memory_info(')}

int main( int argc, char **argv )
{{
    unsigned long long total, used;

    (void)argc;
    fake_total = strtoull( argv[1], NULL, 0 );
    fake_kernel_used = strtoull( argv[2], NULL, 0 );
    fake_free = strtoull( argv[3], NULL, 0 );
    fake_rc = (int)strtol( argv[4], NULL, 0 );
    horizon_get_memory_info( &total, &used );
    printf( "%llu %llu", total, used );
    return 0;
}}
'''


def run(binary, total, kernel_used, free_bytes, rc=0):
    out = subprocess.run([str(binary), str(total), str(kernel_used), str(free_bytes), str(rc)],
                         check=True, capture_output=True, text=True).stdout.split()
    total, used = int(out[0]), int(out[1])
    # kernelbase's GlobalMemoryStatusEx: AvailPageFile is TotalCommitLimit minus
    # TotalCommittedPages, which get_performance_info builds from total - used.
    freeram = total - used
    avail_page_file = (((total + PAGE) // PAGE) - ((total + PAGE - freeram) // PAGE)) * PAGE
    return total, used, avail_page_file


with tempfile.TemporaryDirectory(prefix='wine-nx-memory-status-') as tmp:
    tmp = Path(tmp)
    (tmp / 'test.c').write_text(fixture)
    subprocess.run(['cc', '-g', '-Wall', '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                    str(tmp / 'test.c'), '-o', str(tmp / 'test')], check=True)
    test = tmp / 'test'

    # A Dark Athena run: Horizon said 2048 MB total, 1907 MB used, while the
    # allocator had 900 MB it could still hand out.
    total, used, avail = run(test, 2048 * MB, 1907 * MB, 900 * MB)
    assert total == 2048 * MB, total
    assert total - used == 900 * MB, (total, used)
    assert avail >= NEEDED, avail
    print(f'the run: {(total - used) // MB} MB free, AvailPageFile {avail // MB} MB '
          f'(Fallout needs {NEEDED // MB} MB, Dark Athena stopped at 140 MB)')

    # The build 109 Fallout run, which read 4 MB of AvailPageFile.
    _, _, avail_old = run(test, 3189 * MB, 3185 * MB, 0)
    assert avail_old <= PAGE, avail_old

    # The kernel's figure is not used at all: the same allocator state reports
    # the same free memory whatever Horizon says about the heap.
    assert run(test, 2048 * MB, 0, 900 * MB) == run(test, 2048 * MB, 2047 * MB, 900 * MB)

    # A free figure larger than the grant, and nothing at all, stay bounded.
    assert run(test, 2048 * MB, 1907 * MB, 4096 * MB)[1] == 0
    assert run(test, 0, 0, 900 * MB) == (0, 0, 0)
    # And a failed svcGetInfo leaves nothing to report.
    assert run(test, 2048 * MB, 1907 * MB, 900 * MB, rc=0xd401) == (0, 0, 0)
