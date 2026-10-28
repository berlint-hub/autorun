#!/usr/bin/env python3
"""Set the Large Address Aware flag on a 32-bit program, on the host.

A 32-bit image without IMAGE_FILE_LARGE_ADDRESS_AWARE is given 2 GB of user
address space even on a 64-bit host; with the flag it gets 4 GB. This is the
same edit the Windows tools make -- the flag is one bit in the COFF header --
and it is done here because the usual tools are Windows GUI programs that
cannot run on the console, and because a card's game is a file on the host's
disk anyway.

Wine honours the flag where it matters: `set_large_address_space()` in
`dlls/ntdll/unix/virtual.c` sets the 32-bit limit from it, and the heap size
check in `dlls/kernel32/heap.c` reads the same bit. The runtime says which
ceiling a program was given in its log: `[VA] 32-bit program: address space
up to ...`.

What this does *not* do is get a program past the launcher's "Address space
unavailable" dialog. That one is about the address space of the forwarder
Autorun itself runs in -- a property of the installed NPDM, decided when
Horizon creates the process -- and no bit in a game's header can change it.
Use Settings > System for that: the 32-bit forwarder, or the Atmosphere
low-address patch for the 39-bit one.

Usage:
    large-address-aware.py GAME.EXE              report, change nothing
    large-address-aware.py --set GAME.EXE        set the flag
    large-address-aware.py --clear GAME.EXE      clear it again

Exit codes: 0 done; 2 not a 32-bit PE image (nothing to lift); 1 I/O error.
"""
import struct
import sys

LARGE_ADDRESS_AWARE = 0x0020
PE32 = 0x10b
PE32_PLUS = 0x20b


class Not32Bit(Exception):
    """The file is not a 32-bit PE image, so the flag would mean nothing."""


def header(data):
    """Return (offset of Characteristics, its value, offset of the checksum)."""
    if len(data) < 0x40 or data[:2] != b'MZ':
        raise Not32Bit('not a DOS/PE image')
    pe, = struct.unpack_from('<I', data, 0x3c)
    if pe + 24 > len(data) or data[pe:pe + 4] != b'PE\0\0':
        raise Not32Bit('no PE header where the DOS header points')

    optional_size, = struct.unpack_from('<H', data, pe + 20)
    optional = pe + 24
    if optional_size < 70 or optional + optional_size > len(data):
        raise Not32Bit('truncated optional header')

    magic, = struct.unpack_from('<H', data, optional)
    if magic == PE32_PLUS:
        raise Not32Bit('a 64-bit image: it has no 2 GB ceiling to lift')
    if magic != PE32:
        raise Not32Bit(f'unknown optional header magic {magic:#x}')

    characteristics_offset = pe + 4 + 18
    characteristics, = struct.unpack_from('<H', data, characteristics_offset)
    return characteristics_offset, characteristics, optional + 64


def image_checksum(data, checksum_offset):
    """The PE checksum `data` should carry, its own field left out."""
    copy = bytearray(data)
    struct.pack_into('<I', copy, checksum_offset, 0)

    total = 0
    even = len(copy) - (len(copy) & 1)
    for (word,) in struct.iter_unpack('<H', copy[:even]):
        total += word
        total = (total & 0xffff) + (total >> 16)
    if len(copy) & 1:
        total += copy[-1]
    total = (total & 0xffff) + (total >> 16)
    return (total + len(copy)) & 0xffffffff


def with_flag(data, checksum_offset, characteristics_offset, wanted):
    """The image with the flag as asked for, and its checksum made to match."""
    result = bytearray(data)
    characteristics, = struct.unpack_from('<H', result, characteristics_offset)
    characteristics = (characteristics | LARGE_ADDRESS_AWARE) if wanted else (characteristics & ~LARGE_ADDRESS_AWARE)
    struct.pack_into('<H', result, characteristics_offset, characteristics)
    struct.pack_into('<I', result, checksum_offset, image_checksum(result, checksum_offset))
    return result


def report(name, characteristics):
    state = 'Large Address Aware' if characteristics & LARGE_ADDRESS_AWARE else 'not Large Address Aware'
    ceiling = '4 GB' if characteristics & LARGE_ADDRESS_AWARE else '2 GB'
    print(f'{name}: 32-bit, {state} ({ceiling} of user address space)')


def main(argv):
    wanted = None
    paths = []

    for argument in argv:
        if argument == '--set':
            wanted = True
        elif argument == '--clear':
            wanted = False
        elif argument in ('-h', '--help'):
            print(__doc__.strip())
            return 0
        elif argument.startswith('-'):
            print(f'unknown option {argument}', file=sys.stderr)
            return 2
        else:
            paths.append(argument)

    if not paths:
        print('name at least one executable', file=sys.stderr)
        return 2

    for name in paths:
        try:
            with open(name, 'rb') as source:
                data = source.read()
            characteristics_offset, characteristics, checksum_offset = header(data)
        except Not32Bit as error:
            print(f'{name}: {error}', file=sys.stderr)
            return 2
        except OSError as error:
            print(f'{name}: {error.strerror}', file=sys.stderr)
            return 1

        report(name, characteristics)
        if wanted is None:
            continue
        if bool(characteristics & LARGE_ADDRESS_AWARE) == wanted:
            print(f'{name}: already {"set" if wanted else "clear"}, nothing written')
            continue

        result = with_flag(data, checksum_offset, characteristics_offset, wanted)
        try:
            with open(name, 'wb') as target:
                target.write(result)
        except OSError as error:
            print(f'{name}: {error.strerror}', file=sys.stderr)
            return 1
        print(f'{name}: flag {"set" if wanted else "cleared"}, {len(data)} bytes, '
              f'checksum {image_checksum(result, checksum_offset):#010x}')

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
