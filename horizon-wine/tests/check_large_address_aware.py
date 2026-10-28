#!/usr/bin/env python3
"""`horizon-wine/tools/large-address-aware.py` edits one bit and nothing else.

The tool changes a game's only way to get more than 2 GB of user address
space, so what it must not do is touch anything else: the flag is two bytes
in the COFF header, the checksum is four in the optional header, and the rest
of the image has to come back byte for byte. This builds a PE32 in a
temporary directory, runs the tool over it, and checks exactly that -- plus
the two refusals that keep it from being pointed at the wrong file.
"""
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
tool = root / 'horizon-wine/tools/large-address-aware.py'

LARGE_ADDRESS_AWARE = 0x0020
failures = []


def want(condition, message):
    if not condition:
        failures.append(message)


def pe_image(magic=0x10b, characteristics=0x0102, payload=b'payload!'):
    """A PE image with one section: enough for a header parser, no more."""
    optional_size = 224 if magic == 0x10b else 240
    dos = bytearray(0x40)
    dos[0:2] = b'MZ'
    struct.pack_into('<I', dos, 0x3c, 0x40)

    coff = struct.pack('<HHIIIHH', 0x14c, 1, 0, 0, 0, optional_size, characteristics)
    optional = bytearray(optional_size)
    struct.pack_into('<H', optional, 0, magic)
    section_table = 0x40 + 24 + optional_size
    section = struct.pack('<8sIIIIIIHHI', b'.text', 0x1000, 0x1000, len(payload), section_table + 40,
                          0, 0, 0, 0, 0x60000020)
    image = bytes(dos) + b'PE\0\0' + coff + bytes(optional) + section + payload

    # A checksum in the image, so the tool's own can be compared with one that
    # was written by something other than the code under test.
    data = bytearray(image)
    offset = 0x40 + 24 + 64
    struct.pack_into('<I', data, offset, 0)
    total, even = 0, len(data) - (len(data) & 1)
    for (word,) in struct.iter_unpack('<H', data[:even]):
        total += word
        total = (total & 0xffff) + (total >> 16)
    if len(data) & 1:
        total += data[-1]
    total = (total & 0xffff) + (total >> 16)
    struct.pack_into('<I', data, offset, (total + len(data)) & 0xffffffff)
    return bytes(data)


def run(*arguments):
    return subprocess.run([sys.executable, str(tool), *arguments],
                          capture_output=True, text=True)


def characteristics(data):
    pe, = struct.unpack_from('<I', data, 0x3c)
    return struct.unpack_from('<H', data, pe + 4 + 18)[0]


def checksum_of(data):
    pe, = struct.unpack_from('<I', data, 0x3c)
    offset = pe + 24 + 64
    copy = bytearray(data)
    struct.pack_into('<I', copy, offset, 0)
    total = 0
    even = len(copy) - (len(copy) & 1)
    for (word,) in struct.iter_unpack('<H', copy[:even]):
        total += word
        total = (total & 0xffff) + (total >> 16)
    if len(copy) & 1:
        total += copy[-1]
    total = (total & 0xffff) + (total >> 16)
    return struct.unpack_from('<I', data, offset)[0], (total + len(copy)) & 0xffffffff


def main():
    with tempfile.TemporaryDirectory() as directory:
        game = Path(directory) / 'game.exe'
        original = pe_image()
        game.write_bytes(original)

        result = run(str(game))
        want(result.returncode == 0, f'reporting a 32-bit image failed: {result.stderr}')
        want('not Large Address Aware' in result.stdout, 'the report does not say the flag is clear')
        want('2 GB' in result.stdout, 'the report does not name the ceiling the flag decides')
        want(game.read_bytes() == original, 'reporting a file changed it')

        result = run('--set', str(game))
        want(result.returncode == 0, f'setting the flag failed: {result.stderr}')
        edited = game.read_bytes()
        want(len(edited) == len(original), 'the image changed length')
        want(characteristics(edited) & LARGE_ADDRESS_AWARE, 'the flag is not set in the header')
        stored, computed = checksum_of(edited)
        want(stored == computed, f'the checksum was not updated: {stored:#x} != {computed:#x}')

        pe, = struct.unpack_from('<I', original, 0x3c)
        changed = {i for i in range(len(original)) if original[i] != edited[i]}
        allowed = {pe + 4 + 18, pe + 4 + 19} | set(range(pe + 24 + 64, pe + 24 + 68))
        want(changed <= allowed, f'the edit changed bytes outside the flag and the checksum: {sorted(changed - allowed)}')

        result = run('--set', str(game))
        want(result.returncode == 0 and 'already set' in result.stdout,
             f'setting the flag twice is not a no-op: {result.stdout}{result.stderr}')
        want(game.read_bytes() == edited, 'setting the flag twice rewrote the file')

        result = run('--clear', str(game))
        want(result.returncode == 0, f'clearing the flag failed: {result.stderr}')
        want(not characteristics(game.read_bytes()) & LARGE_ADDRESS_AWARE, 'the flag is still set')
        stored, computed = checksum_of(game.read_bytes())
        want(stored == computed, 'the checksum was not updated when clearing')

        sixty_four = Path(directory) / 'game64.exe'
        sixty_four.write_bytes(pe_image(magic=0x20b))
        before = sixty_four.read_bytes()
        result = run('--set', str(sixty_four))
        want(result.returncode == 2, f'a 64-bit image was accepted: {result.returncode}')
        want('64-bit' in result.stderr, 'the refusal does not say why')
        want(sixty_four.read_bytes() == before, 'a refused file was written')

        text = Path(directory) / 'readme.txt'
        text.write_text('not an image')
        result = run('--set', str(text))
        want(result.returncode == 2, f'a text file was accepted: {result.returncode}')

        truncated = Path(directory) / 'short.exe'
        truncated.write_bytes(original[:0x30])
        result = run(str(truncated))
        want(result.returncode == 2, f'a truncated header was accepted: {result.returncode}')

    if failures:
        for message in failures:
            print(f'large address aware: {message}', file=sys.stderr)
        return 1
    print('large address aware: one bit and the checksum, nothing else in the image')
    return 0


if __name__ == '__main__':
    sys.exit(main())
