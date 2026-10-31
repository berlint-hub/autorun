#!/usr/bin/env python3
"""A program that keeps dying gets one start with the safe fast paths.

The count of starts that did not come back lives next to the program's settings
(start_guard.h). This checks the three points where the runtime has to meet it,
because a guard that is read but never cleared -- or cleared on a crash -- is
worse than none: it would either do nothing, or turn a game safe that was never
broken. tests/start_guard.c runs the counting itself on the host.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
runtime = (root / 'horizon-wine/source/runtime.c').read_text()

# Read where the program's settings are read, and before any of them is used.
read_at = runtime.index('launcher_settings_read( &kv, &settings );')
guard_at = runtime.index('apply_start_guard( &settings, settings_path, target );')
used_at = runtime.index('horizon_fast_sync_enabled = settings.fast_sync;', read_at)
assert read_at < guard_at < used_at, \
    'the guard is not applied between reading the settings and using them'

# The bundle is the Compatibility one, and it may not choose a translator: the
# mask is what the launcher passes for a card with none installed, and a start
# nobody asked for is not the place to switch translators.
assert 'launcher_preset_apply( settings, LAUNCHER_PRESET_COMPATIBILITY, 0 )' in runtime, \
    'the safe start no longer applies the Compatibility bundle with the translator left alone'

# Counted for the start about to begin, so a start that does not come back is
# counted by the next one.
assert 'nx_start_guard_write( runtime_start_guard, nx_start_guard_next( streak ) )' in runtime, \
    'the start is not counted before the program runs'

# Cleared by a start that came home, and only by one: a crash is what the count
# is for.
launcher_at = runtime.index('static int return_to_launcher( void )')
# The function ends at the first brace of its own column; its blocks are indented.
launcher_end = runtime.index('\n}\n', launcher_at)
block = runtime[launcher_at:launcher_end]
assert 'nx_start_guard_clear( runtime_start_guard )' in block, \
    'a start that came home no longer clears the count'
assert 'nx_start_guard_crash( wine_nx_program_exit_code )' in block, \
    'the count is cleared whatever the program ended on, so a crash would clear it'
assert block.index('nx_start_guard_crash') < block.index('nx_start_guard_clear'), \
    'the clear is not guarded by how the program ended'

# And the line reaches the summary a player is asked to send with a report.
assert '"[STABILITY]"' in runtime, \
    'what the safe start said is not in the run report'

print('Safe start: counted beside the settings, applied before they are used '
      '(Compatibility, translator left alone), cleared by a clean end only, and in the report')
