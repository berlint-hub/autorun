from pathlib import Path
import os
import struct
import subprocess
import tempfile
from zipfile import ZipFile, ZIP_DEFLATED

horizon_wine = Path(__file__).resolve().parents[1]
deps = Path(os.environ.get('AUTORUN_HOST_DEPS', '/tmp/autorun-host-deps/root/usr'))
managed = ['wine-nx-runtime.nro', 'drive_c/windows/system32/ntdll.dll',
           'drive_c/windows/system32/kernel32.dll', 'drive_c/windows/system32/kernelbase.dll',
           'drive_c/windows/system32/winebox64ec.dll', 'drive_c/windows/syswow64/ntdll.dll',
           'drive_c/windows/syswow64/kernel32.dll', 'drive_c/windows/syswow64/kernelbase.dll',
           'drive_c/dxvk64/dxgi.dll']
preserved = ['config/launcher.txt', 'registry/user.reg', 'config/system.reg',
             'drive_c/users/Switch/save.dat', 'drive_c/windows/custom.ini']


def data(name, marker):
    if name.endswith('.nro'):
        result = bytearray(4096)
        result[16:20] = b'NRO0'
        struct.pack_into('<I', result, 24, len(result))
        result[128:128 + len(marker)] = marker
        return bytes(result)
    return marker + name.encode()


with tempfile.TemporaryDirectory(prefix='autorun-runtime-update-') as temporary:
    temporary = Path(temporary)
    executable = temporary / 'test'
    command = [os.environ.get('CC', 'cc'), '-std=gnu11', '-D_GNU_SOURCE', '-Wall', '-Wextra', '-Werror',
               '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    if deps.is_dir():
        command += ['-I' + str(deps / 'include'), '-L' + str(deps / 'lib/aarch64-linux-gnu'),
                    '-Wl,-rpath,' + str(deps / 'lib/aarch64-linux-gnu')]
    command += [str(horizon_wine / 'tests/autorun_install.c'), '-lminizip', '-lz', '-Wl,--wrap=rename', '-Wl,--wrap=unlink', '-o', str(executable)]
    subprocess.run(command, check=True)
    environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
    archive_path = temporary / 'full-update.zip'
    with ZipFile(archive_path, 'w', ZIP_DEFLATED) as archive:
        for name in managed + preserved:
            archive.writestr('switch/wine/' + name, data(name, b'new'))
    for failure in (0, 1):
        root = temporary / ('rollback' if failure else 'install')
        for name in managed + preserved:
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data(name, b'old'))
        subprocess.run([str(executable), str(root), str(archive_path), str(failure)], check=True, env=environment)
        for name in managed:
            assert (root / name).read_bytes() == data(name, b'old' if failure else b'new'), name
        for name in preserved:
            assert (root / name).read_bytes() == data(name, b'old'), name
        assert not (root / 'updates/transaction/journal').exists()
    # The card archive (tools/package-autorun.py) as the build workflow
    # releases it: the runtimes and their files, and no Windows modules, which
    # the DLL repository brings. The card's modules stay as they are.
    card = ['wine-nx-runtime.nro', 'wine-nx-runtime-nxvk.nro', 'wine-nx-runtime.nro.sym',
            'wine-nx-runtime-nxvk.nro.sym', 'README.txt', 'game-fixes/bundled.json',
            'drive_c/windows/autorun-setup.exe', 'licenses/nxvk/COPYING', 'build-manifest.json']
    settings = ['config/settings.json', 'config/keys.txt']
    modules = ['drive_c/windows/system32/ntdll.dll', 'drive_c/windows/syswow64/kernel32.dll']
    card_path = temporary / 'card.zip'
    with ZipFile(card_path, 'w', ZIP_DEFLATED) as archive:
        for name in card + settings:
            archive.writestr('switch/wine/' + name, data(name, b'new'))
        archive.writestr('switch/wine/drive_c/WarCraft III/', b'')
    root = temporary / 'card'
    for name in card + settings + modules:
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data(name, b'old'))
    subprocess.run([str(executable), str(root), str(card_path), '0'], check=True, env=environment)
    for name in card:
        assert (root / name).read_bytes() == data(name, b'new'), name
    for name in settings + modules:
        assert (root / name).read_bytes() == data(name, b'old'), name
    assert (root / 'drive_c/WarCraft III').is_dir()
    # An archive with some of the Windows modules and not the rest is refused:
    # it would leave the card with half a system.
    half_path = temporary / 'half.zip'
    with ZipFile(half_path, 'w', ZIP_DEFLATED) as archive:
        for name in ['wine-nx-runtime.nro', 'drive_c/windows/system32/ntdll.dll']:
            archive.writestr('switch/wine/' + name, data(name, b'new'))
    root = temporary / 'half'
    for name in ['wine-nx-runtime.nro', 'drive_c/windows/system32/ntdll.dll']:
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data(name, b'old'))
    subprocess.run([str(executable), str(root), str(half_path), '2'], check=True, env=environment)
    for name in ['wine-nx-runtime.nro', 'drive_c/windows/system32/ntdll.dll']:
        assert (root / name).read_bytes() == data(name, b'old'), name
    # The card archive on a card whose runtime keeps its symbol file open,
    # which Horizon then neither deletes nor replaces, as a runtime until
    # 22524fc kept wine-nx-runtime.sym (the launcher now closes it for the
    # update): the update fails, and undoing it leaves that file as it was
    # rather than failing on it too, at once (3) or at the next start when
    # undoing it stopped half way (4). Each time the card is as before and no
    # transaction is left to block the runtime.
    for mode in (3, 4):
        root = temporary / f'held-{mode}'
        for name in card:
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data(name, b'old'))
        subprocess.run([str(executable), str(root), str(card_path), str(mode)], check=True, env=environment)
        for name in card:
            assert (root / name).read_bytes() == data(name, b'old'), (mode, name)
        assert not (root / 'updates/transaction').exists(), mode
    # The card archive on a card of 22524fc or before, whose runtime keeps
    # wine-nx-runtime.sym open: the archive's symbol files are
    # wine-nx-runtime.nro.sym and wine-nx-runtime-nxvk.nro.sym, so the open
    # file is not in the way and stays as it is (the new runtime removes it).
    older = ['README.txt', 'build-manifest.json', 'wine-nx-runtime-nxvk.nro', 'wine-nx-runtime-nxvk.sym',
             'wine-nx-runtime.nro', 'wine-nx-runtime.sym']
    root = temporary / 'held-5'
    for name in older:
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data(name, b'old'))
    subprocess.run([str(executable), str(root), str(card_path), '5'], check=True, env=environment)
    for name in card:
        assert (root / name).read_bytes() == data(name, b'new'), name
    assert (root / 'wine-nx-runtime.sym').read_bytes() == data('wine-nx-runtime.sym', b'old')
    assert not (root / 'updates/transaction').exists()
    print('app update: NRO and DLLs refreshed; settings, registry and saves preserved; rollback restored originals; '
          'the card archive without Windows modules installs both runtimes and their files, half a system is refused; '
          'an open file the update cannot replace fails it and is left alone in undoing it, now or at the next '
          'start, and the card archive brings its symbol files beside it')
