"""The llvm-mingw this tree builds Wine's Windows side with, on any machine.

build.sh fetches the one its host needs -- llvm-mingw names its archives by
platform, so a Mac and a Linux runner have different folders under
toolchains/ -- and hands the path over in $WINE_NX_LLVM_MINGW. A build that
ran some other way left it in toolchains/, and a machine with its own has it
on PATH. The tools inside are the same everywhere, which is what the callers
want: i686-w64-mingw32-clang for Autorun's setup programs, llvm-readobj for
reading what the package holds.
"""
from pathlib import Path
import os
import shutil
import sys

horizon_wine = Path(__file__).resolve().parents[1]
_tools = ('i686-w64-mingw32-clang', 'llvm-readobj')


def bin_dir(what: str = _tools[0]) -> Path:
    """The folder holding `what`, or the end of the program with a sentence."""
    named = os.environ.get('WINE_NX_LLVM_MINGW')
    if named:
        return Path(named) / 'bin'
    for candidate in sorted(horizon_wine.glob('toolchains/llvm-mingw-*/bin')):
        if (candidate / what).exists():
            return candidate
    found = shutil.which(what)
    if found:
        return Path(found).parent
    sys.exit(f'{what} is not in an llvm-mingw this tree knows: run build.sh once, '
             'set WINE_NX_LLVM_MINGW, or put one on PATH')
