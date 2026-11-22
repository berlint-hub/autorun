#!/usr/bin/env python3
"""Which release the launcher's updater offers (source/autorun_update.c and
source/launcher_update.c).

The card's archive is released in the repository the runtime is built from,
autorun-<run>-<commit> with autorun.zip, beside Carafe's FEX and DXVK releases,
which are newer as often as not. The updater reads the repository's release
list, newest first, and takes the newest card release: Carafe's, drafts,
prereleases, an archive in another repository and notes longer than the
launcher holds are passed over. A release made from the commit the runtime
was built from is not offered again. The functions are built as the sources
have them; the list is made here in the shape GitHub gives it."""
import json
import os
import pathlib
import re
import shutil
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
source = (root / 'horizon-wine/source/autorun_update.c').read_text()
launcher = (root / 'horizon-wine/source/launcher_update.c').read_text()
workflow = (root / '.github/workflows/build.yml').read_text()

repo = re.search(r'#define WINE_NX_UPDATE_REPO "([^"]+)"', source).group(1)
assert 'releases?per_page=' in source and 'releases/latest' not in source
# The workflow releases what the updater looks for: the tag's prefix and the
# archive's name, made latest, from pushes to main only.
assert 'autorun-${{ github.run_number }}-' in workflow and 'autorun.zip' in workflow
assert "github.event_name == 'push' && github.ref == 'refs/heads/main'" in workflow

start = source.index('#ifndef WINE_NX_UPDATE_REPO')
defines = source[start:source.index('\n', source.index('#define DOWNLOAD_PREFIX', start))] + '\n' + \
    re.search(r'#define ARCHIVE_MAX .*', source).group(0) + '\n'
asset = source[source.index('struct asset\n{'):source.index('struct download_stream')]
parsing = source[source.index('static int digest('):source.index('static enum autorun_update_result request_metadata(')]
same_commit = launcher[launcher.index('static int same_commit('):launcher.index('static int new_release(')]

test = r'''
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "autorun_update.h"
#include "json_reader.h"
''' + defines + '\n#define WINE_NX_BUILD_ID "a422bdf"\n' + asset + parsing + same_commit + r'''
static unsigned char *slurp( const char *path, size_t *size )
{
    FILE *f = fopen( path, "rb" );
    unsigned char *data;
    long length;
    assert( f && !fseek( f, 0, SEEK_END ) && (length = ftell( f )) >= 0 && !fseek( f, 0, SEEK_SET ) );
    data = malloc( length + 1 );
    assert( data && fread( data, 1, length, f ) == (size_t)length );
    fclose( f );
    *size = length;
    return data;
}

int main( int argc, char **argv )
{
    static struct autorun_release release;
    size_t size;
    int i;

    for (i = 1; i + 2 < argc; i += 3)
    {
        unsigned char *data = slurp( argv[i], &size );
        int expected = atoi( argv[i + 1] ), got = parse_release_list( data, size, &release );
        if (got != expected || (got == 1 && strcmp( release.tag, argv[i + 2] )))
        {
            fprintf( stderr, "%s: %d %s, expected %d %s\n", argv[i], got, got == 1 ? release.tag : "", expected, argv[i + 2] );
            return 1;
        }
        if (got == 1)
            assert( release.size == 41000000 && strlen( release.digest ) == 64 &&
                    !strncmp( release.url, DOWNLOAD_PREFIX, strlen( DOWNLOAD_PREFIX ) ) );
        free( data );
    }
    assert( same_commit( "autorun-226-a422bdfb" ) && same_commit( "autorun-226-a422bdf" ) &&
            same_commit( "autorun-9-A422BDFBD419" ) );
    assert( !same_commit( "autorun-227-b01adad" ) && !same_commit( "autorun-226-a42" ) &&
            !same_commit( "fex-carafe-2611-3" ) && !same_commit( "autorun" ) && !same_commit( "autorun-226-a422bdx" ) );
    return 0;
}
'''

prefix = f'https://github.com/{repo}/releases/download/'
user = {'login': 'someone', 'id': 1, 'type': 'User', 'site_admin': False, 'avatar_url': 'https://avatars.example/u/1'}


def release(tag, assets, draft=False, prerelease=False, notes='What changed.', published='2026-11-22T16:00:00Z'):
    return {'url': f'https://api.github.com/repos/{repo}/releases/1', 'id': 1, 'author': user, 'node_id': 'RE_x',
            'tag_name': tag, 'target_commitish': 'main', 'name': tag, 'draft': draft, 'immutable': False,
            'prerelease': prerelease, 'created_at': published, 'updated_at': published, 'published_at': published,
            'assets': assets, 'tarball_url': None, 'zipball_url': None, 'body': notes,
            'reactions': {'+1': 0, 'total_count': 0}}


def archive(tag, name='autorun.zip', where=prefix, digest='sha256:' + 'ab' * 32):
    return {'url': 'https://api.github.com/assets/1', 'id': 2, 'node_id': 'RA_x', 'name': name, 'label': '',
            'uploader': user, 'content_type': 'application/zip', 'state': 'uploaded', 'size': 41000000,
            'digest': digest, 'download_count': 3, 'created_at': '2026-11-22T16:00:00Z',
            'updated_at': '2026-11-22T16:00:00Z', 'browser_download_url': f'{where}{tag}/{name}'}


fex = [release(t, [archive(t, f'{t}.tar.gz')]) for t in ('fex-experimental-2611-3', 'fex-carafe-2611-3',
                                                          'dxvk-carafe-3.1.1-1')]
lists = {
    'card among Carafe releases': (fex + [
        release('autorun-230-0123456', [archive('autorun-230-0123456')], draft=True),
        release('autorun-229-1234567', [archive('autorun-229-1234567')], prerelease=True),
        release('autorun-228-2345678', [archive('autorun-228-2345678', where='https://github.com/other/autorun/releases/download/')]),
        release('autorun-227-3456789', [archive('autorun-227-3456789')], notes='x' * 20000),
        release('autorun-226-a422bdf', [archive('autorun-226-a422bdf'), archive('autorun-226-a422bdf', 'symbols.txt')]),
        release('autorun-225-b01adad', [archive('autorun-225-b01adad')], published='2026-11-21T09:00:00Z'),
    ], 1, 'autorun-226-a422bdf'),
    # GitHub orders by the numbers in the tags as much as by date: an older
    # card before the newest, and Carafe's before both. The newest published
    # is offered, wherever it is in the list.
    'newest card not first': (fex + [
        release('autorun-225-b01adad', [archive('autorun-225-b01adad')], published='2026-11-21T09:00:00Z'),
        release('autorun-99-c0ffee1', [archive('autorun-99-c0ffee1')], published='2026-11-20T09:00:00Z'),
        release('autorun-228-d9ef853', [archive('autorun-228-d9ef853')], published='2026-11-22T19:19:47Z'),
        release('autorun-227-3456789', [archive('autorun-227-3456789')], published='2026-11-22T18:00:00Z'),
    ], 1, 'autorun-228-d9ef853'),
    'Carafe releases only': (fex, 0, '-'),
    'no releases': ([], 0, '-'),
    'card without a digest': ([release('autorun-226-a422bdf', [archive('autorun-226-a422bdf', digest=None)])], 0, '-'),
    # Of two archives the one named autorun.zip; of two others, neither.
    'autorun.zip beside another': ([release('autorun-226-a422bdf', [archive('autorun-226-a422bdf', 'other.zip'),
                                                                    archive('autorun-226-a422bdf')])], 1, 'autorun-226-a422bdf'),
    'two other archives': ([release('autorun-226-a422bdf', [archive('autorun-226-a422bdf', 'one.zip'),
                                                            archive('autorun-226-a422bdf', 'other.zip')])], 0, '-'),
}
cc = os.environ.get('WINE_NX_HOST_CC') or shutil.which('clang') or 'cc'
with tempfile.TemporaryDirectory(prefix='autorun-update-releases-') as directory:
    path = pathlib.Path(directory)
    args = []
    for n, (label, (items, expected, tag)) in enumerate(lists.items()):
        (path / f'{n}.json').write_text(json.dumps(items, indent=2))
        args += [str(path / f'{n}.json'), str(expected), tag]
    (path / 'bad.json').write_text('[{"tag_name": "autorun-1-1234567"},')
    args += [str(path / 'bad.json'), '-1', '-']
    (path / 'object.json').write_text(json.dumps(release('autorun-226-a422bdf', [archive('autorun-226-a422bdf')])))
    args += [str(path / 'object.json'), '-1', '-']
    (path / 'test.c').write_text(test)
    subprocess.run([cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-g', '-O1',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I', str(root / 'horizon-wine/source'), str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    run = subprocess.run([str(path / 'test'), *args], capture_output=True, text=True)
    if run.returncode:
        raise SystemExit(run.stderr.strip()[-2000:])
print(f'autorun updates: the newest published card release of {repo} is offered, wherever the list has it, '
      'among Carafe\'s, drafts, prereleases, '
      'foreign archives and long notes; none, no digest, two archives neither named autorun.zip and broken lists are not; '
      'the build\'s own commit is not offered again')
