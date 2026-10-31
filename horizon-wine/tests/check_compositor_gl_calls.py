#!/usr/bin/env python3
"""The GL calls the compositor's drawing makes, counted on stubs.

compositor_gl.c is compiled against tests/gl-shims (the few OpenGL names it
uses) and a table of counting stubs, so this runs on any machine: what it
checks is which driver calls a frame costs, not what the pixels come out as.
That is tests/compositor_gl_test.c's job, on a Mac's real OpenGL.
"""
from pathlib import Path
import subprocess
import tempfile

import test_support

root = Path(__file__).resolve().parents[2]
tests = root / 'horizon-wine/tests'
source = root / 'horizon-wine/source'

with tempfile.TemporaryDirectory(prefix='autorun-compositor-gl-') as tmp:
    binary = Path(tmp) / 'compositor_gl_calls'
    subprocess.run([test_support.compiler(), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(tests / 'gl-shims'), '-I' + str(source),
                    str(tests / 'compositor_gl_calls.c'), str(source / 'compositor_gl.c'),
                    '-o', str(binary)], check=True)
    print(subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout.strip())
