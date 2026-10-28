#!/usr/bin/env python3
"""The FEX-2610 memory-layer patch: made against one commit, and carrying this ABI.

`horizon-wine/fex/fex-2610-horizon-memory.patch` is the port a builder applies
to `berlint-hub/FEX@6ec29a2e` before compiling the tuned modules for the card.
Two things about it have to stay true, and both are properties of files in
this repository -- no FEX tree is needed to check them:

* it is a patch against that commit and nothing else: every hunk names a file
  that exists there (the files it creates are the ones FEX-2610 lacks), and
  the four ABI headers it installs into the FEX tree are byte-identical to
  `horizon-wine/fex/`. A module built against a different header does not find
  the runtime's opcodes, which is the failure that reads as
  `STATUS_DLL_INIT_FAILED` at startup.

* it installs a working memory layer rather than the stubs FEX-2610 shipped:
  the JIT allocation, the I-cache flush and the W^X transition all go through
  the unixlib, and the CRT heap is the runtime's.

* it carries the parts of the FEX work's FEX-2609 port that FEX-2610 did not
  pick up: the decoder's self-modifying-code rule, POP into memory, a
  protection value that is actually read back, an execute fault not taken for
  a write, an allocation that has to land below 2 GB, and a thread that is put
  back after being suspended. FEX-2610 kept some of the scaffolding for these
  (`ExecutableRangeWriteTracked`, `ForceFullSMCDetection`) and lost the use.

The handover that tells a builder to apply it is checked too, so the three
steps and the honest "not built yet" cannot quietly drop out of the docs.
"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[2]
fex = root / 'horizon-wine/fex'
patch = (fex / 'fex-2610-horizon-memory.patch').read_text()
handover = (fex / 'builder-handover.md').read_text()
variants = (fex / 'variants-build.md').read_text()

failures = []


def want(condition, message):
    if not condition:
        failures.append(message)


# -- the patch is a unified diff over the files it names ---------------------
files = re.findall(r'^diff --git a/(\S+) b/\S+$', patch, re.MULTILINE)
want(len(files) > 20, f'the patch touches {len(files)} files; the port is larger than that')
want(len(files) == len(set(files)), 'the patch names a file twice')
for path in files:
    want(not path.startswith('/') and '..' not in Path(path).parts,
         f'the patch writes outside the FEX tree: {path}')
    want(('--- a/' + path) in patch, f'{path} has no "--- a/" line')

def section(path):
    """The part of the patch that is about one file."""
    start = patch.index(f'diff --git a/{path} b/{path}\n')
    end = patch.find('\ndiff --git ', start + 1)
    return patch[start:] if end < 0 else patch[start:end]

created = set(re.findall(r'^--- /dev/null\n\+\+\+ b/(\S+)$', patch, re.MULTILINE))
want(created == set(), f'the patch creates {sorted(created)}; FEX-2610 has stubs for all of these')

# -- the ABI it installs is this repository's, byte for byte -----------------
# Each of the four is a stub in FEX-2610 that the patch replaces whole: one
# hunk from line 1, so its context and added lines together are the new file.
def rebuilt(name):
    body = section(f'Source/Windows/include/{name}')
    want(body, f'the patch does not install Source/Windows/include/{name}')
    hunk = re.search(r'^@@ -1,(\d+) \+1,(\d+) @@', body, re.MULTILINE)
    want(hunk, f'{name}: the patch does not replace the file in one hunk')
    if not hunk:
        return None
    lines = [line[1:] for line in body[hunk.end():].split('\n')
             if line[:1] in (' ', '+') and not line.startswith('+++')]
    want(len(lines) == int(hunk.group(2)),
         f'{name}: {len(lines)} lines result, the hunk says {hunk.group(2)}')
    return '\n'.join(lines).strip()


for name in ('unixlib.h', 'jit_registry.h', 'code_storage.h', 'cache_policy.h'):
    ours = (fex / name).read_text().strip()
    want(rebuilt(name) == ours, f'the {name} the patch installs is not this repository\'s')

# The one thing the policy must not reintroduce: a 32-bit shift of a 32-bit 1,
# which makes a 32-bit guest's memory size zero.
want('static_cast<uint32_t>(1) << 32' not in (rebuilt('cache_policy.h') or ''),
     'the installed cache_policy.h brings back the 1u << 32 (UB, and a 0 size)')

# -- the memory layer, not the stubs ----------------------------------------
for needle, why in [
    ('WINE_NX_FEX_ALLOC', 'the JIT allocation does not go through the unixlib'),
    ('WINE_NX_FEX_FLUSH', 'the I-cache flush does not go through the unixlib'),
    ('WINE_NX_FEX_PROTECT_CODE', 'code protection does not go through the unixlib'),
    ('WINE_NX_FEX_HEAP_ALLOC', 'the CRT heap is not the runtime\'s'),
    ('HorizonInitialize', 'nothing initializes the Horizon layer at process start'),
    ('HorizonAttachThread', 'no thread gets the exception stack it reports on'),
    ('PrepareNativeWrite', 'the write a file read is about to make is not prepared for'),
]:
    want(needle in patch, why)

# -- what the FEX work's FEX-2609 port does and FEX-2610 does not ------------
# Each marker only exists when the carried piece is there, so a re-port that
# silently drops one of them fails here rather than on a card.
for needle, why in [
    ('ExecutableRangeWriteTracked = RangeInfo.WriteTracked',
     'nothing records whether a writable range is write-tracked'),
    ('BlockIt->ForceFullSMCDetection = true',
     'a writable, untracked range no longer forces the full SMC check'),
    ('StackBase',
     'POP into memory does not account for the RSP increment'),
    ('Prot, &Prot', 'the module write restores protection from an uninitialised value'),
    ('0x7ffffffb', 'the 32-bit bridge is allocated without checking it landed below 2 GB'),
    ('ExceptionInformation[0] == 1', 'an execute fault is taken for self-modifying code'),
    ('NtResumeThread', 'a thread this code suspended is left suspended'),
]:
    want(needle in patch, why)

want('__builtin___clear_cache' not in patch.split('jit_registry.h')[0] or
     'HorizonFlushICache' in patch,
     'the patch flushes the instruction cache with __builtin___clear_cache alone')
want(patch.count('\n+option(FEX_HORIZON') == 1,
     'the patch leaves more than one FEX_HORIZON option in the tree')
want('\n+  message(FATAL_ERROR' not in patch,
     'the patch keeps the FATAL_ERROR that made the bundled ABI unreachable')

# -- the handover says the three steps, and what is not proven --------------
for needle, why in [
    ('arena/97e282a9-autorun', 'the handover does not say which runtime branch to take'),
    ('fex-2610-horizon-memory.patch', 'the handover does not name the patch'),
    ('6ec29a2e', 'the handover does not pin the FEX commit'),
    ('FEX_TUNE_TEGRA_X1', 'the handover does not say how the modules are tuned'),
    ('libwow64fex-opt.dll', 'the handover does not say which modules the card gets'),
    ('Not checked', 'the handover does not say what has not been built'),
    ('not carried', 'the handover does not say what the 2609 port has and this one does not'),
]:
    want(needle in handover, why)

# variants-build.md points at the patch instead of saying the layer is owed.
want('fex-2610-horizon-memory.patch' in variants,
     'variants-build.md still says the Horizon memory layer is missing, with no patch named')

if failures:
    for message in failures:
        print(f'fex 2610 patch: {message}', file=sys.stderr)
    sys.exit(1)
print('FEX 2610 memory patch: made against 6ec29a2e, carries this ABI, and the handover names it')
