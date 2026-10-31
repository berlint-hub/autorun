#!/usr/bin/env python3
"""The translator's code cache is kept out of the card's read cache.

FEX's database is a file its module looks a block up in, a record at a time and
by address. Through the read cache a lookup would pull a whole 128 KB chunk off
the card, and take a chunk from the game's own data while doing it -- so the
runtime names that directory and the device reads such a file as its reader
asked. This checks the two halves are still joined: the runtime naming the
translator's directory (the one the launcher measures and clears, fex_cache.h),
and the device leaving a file under it uncached. tests/sd_cache_device.c runs
the device over a fake card and checks the bytes it asks the card for.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
runtime = (root / 'horizon-wine/source/runtime.c').read_text()
device = (root / 'horizon-wine/source/sd_cache.c').read_text()
header = (root / 'horizon-wine/source/sd_read_cache.h').read_text()

# The runtime installs the cache before anything on the card is opened, and
# right after it names the translator's caches -- both of them, the local and
# the roaming half, because a module may keep its database in either.
at = runtime.index('wine_nx_sd_cache_install()')
block = runtime[at:at + 1400]
assert 'wine_nx_sd_cache_exclude' in block, \
    'the runtime no longer names the translator\'s code cache to the card cache'
assert 'fex_cache_root' in block, \
    'the directory named is not the one fex_cache.h gives the launcher'
assert re.search(r'for\s*\(\s*roaming\s*=\s*0;\s*roaming\s*<\s*2;\s*roaming\+\+\s*\)', block), \
    'only one half of the translator\'s cache is named (Local and Roaming both hold one)'
assert block.index('sd_cache_install') < block.index('sd_cache_exclude'), \
    'the directory is named before the cache that has to know about it is installed'

# The device keeps its own list of such directories and leaves a file under one
# uncached, which is what turns off the chunk read and keeps it out of the pool.
assert 'void wine_nx_sd_cache_exclude( const char *dir )' in device, \
    'sd_cache.c no longer lets the runtime name a directory to leave alone'
assert 'sd_cache_path_under' in device and 'sd_cache_path_under' in header, \
    'the comparison that decides what is under a directory is gone'
open_at = device.index('static int sd_cache_open(')
open_end = device.index('static int sd_cache_close(', open_at)
assert 'sd_cache_path_excluded( path )' in device[open_at:open_end], \
    'sd_cache_open() no longer keeps a file under an excluded directory uncached'
assert re.search(r'sd_cache_path_excluded\( path \)\)\s*file->cacheable\s*=\s*0;',
                 device[open_at:open_end]), \
    'the file under an excluded directory is still cached'

print('SD card cache: the translator\'s code cache is named by the runtime (both halves) '
      'and read without the card cache\'s chunks')
