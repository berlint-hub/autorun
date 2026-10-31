#!/usr/bin/env python3
"""What a check needs from the machine it runs on, and what it does without it.

A check that cannot build, or cannot reach the files it examines, has found
nothing wrong with Autorun; it has found a machine that is not the build
machine. It says what is missing and exits 0, so the loop from
documentation/technical.md -- `for t in horizon-wine/tests/check_*.py; do
python3 "$t"; done` -- stays green on a laptop and turns red only when the
tree is wrong. A check that does run and fails exits 1, as it should.

The compiler is `$CC` when it is set, then clang (which the host tests were
written against), then cc and gcc. `WINE_NX_HOST_CC` and `WINE_NX_HOST_CXX`
name one directly, as they did before.
"""
from __future__ import annotations

import os
import platform
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HORIZON_WINE = ROOT / 'horizon-wine'
TESTS = HORIZON_WINE / 'tests'


def skip(reason: str) -> None:
    """Leave the check without a verdict, and leave the run green."""
    print('SKIP: ' + reason)
    raise SystemExit(0)


def _usable(name: str | None) -> str | None:
    if not name:
        return None
    return name if shutil.which(name) else None


def compiler() -> str:
    """The C compiler the host tests build with."""
    for name in (os.environ.get('CC'), 'clang', 'cc', 'gcc'):
        if (found := _usable(name)):
            return found
    skip('no C compiler on PATH: set CC, or install clang or gcc')


def cxx_compiler() -> str:
    """The C++ compiler, for the few checks that build C++."""
    for name in (os.environ.get('CXX'), 'clang++', 'g++', 'c++'):
        if (found := _usable(name)):
            return found
    skip('no C++ compiler on PATH: set CXX, or install clang++ or g++')


def host_cc() -> str:
    """`WINE_NX_HOST_CC`, or the compiler this machine has."""
    return _usable(os.environ.get('WINE_NX_HOST_CC')) or compiler()


def host_cxx() -> str:
    """`WINE_NX_HOST_CXX`, or the C++ compiler this machine has."""
    return _usable(os.environ.get('WINE_NX_HOST_CXX')) or cxx_compiler()


def have_tool(name: str) -> bool:
    return shutil.which(name) is not None


def need_tool(name: str, why: str) -> str:
    if not have_tool(name):
        skip(f'needs {name} ({why})')
    return name


def path(exists: Path | str, why: str) -> Path:
    """The path, when it is there this checkout has what the check reads."""
    found = Path(exists)
    if not found.exists():
        skip(f'needs {found} ({why})')
    return found


def need_arm64(why: str = 'the check compiles AArch64 code') -> None:
    machine = platform.machine().lower()
    if machine not in ('arm64', 'aarch64'):
        skip(f'needs an ARM64 host, this is {platform.machine()} ({why})')


def add_path(where: Path | str) -> Path:
    """Put a folder of Python modules on the path, when it is checked out."""
    found = path(where, 'the module it holds is checked out beside this tree')
    if str(found) not in sys.path:
        sys.path.insert(0, str(found))
    return found


def probe(source: str, flags: list[str] | None = None, cc: str | None = None) -> bool:
    """Compile (and link, unless the source says otherwise) a scrap of C."""
    with tempfile.TemporaryDirectory(prefix='autorun-probe-') as folder:
        unit = Path(folder) / 'probe.c'
        unit.write_text(source)
        command = [cc or compiler(), *(flags or []), str(unit), '-o', str(Path(folder) / 'probe')]
        done = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return done.returncode == 0


def need_header(header: str, why: str = '') -> None:
    """A development header on this machine, such as openssl/sha.h."""
    extra = f' ({why})' if why else ''
    if not probe(f'#include <{header}>\nint main(void) {{ return 0; }}\n'):
        skip(f'needs the {header} development header{extra}')


def need_library(library: str, why: str = '') -> None:
    """A library to link against, such as minizip or z."""
    extra = f' ({why})' if why else ''
    source = 'int main(void) { return 0; }\n'
    with tempfile.TemporaryDirectory(prefix='autorun-probe-') as folder:
        unit = Path(folder) / 'probe.c'
        unit.write_text(source)
        done = subprocess.run([compiler(), str(unit), f'-l{library}', '-o', str(Path(folder) / 'probe')],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if done.returncode != 0:
        skip(f'needs lib{library} to link{extra}')


def run(binary: Path | str, *arguments: str, **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run([str(binary), *arguments], check=True, **kwargs)
