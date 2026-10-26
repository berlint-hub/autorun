#!/usr/bin/env python3
"""Where the tuned FEX modules come from: the FEX work's release, never a build here.

The tuning, the source tree and the compile belong to the FEX work
(`arena/21bcd47b-autorun`, publishing from `berlint-hub/FEX`). This repository
takes the published `fex-opt` release, checks it against its checksums, and
ships it: into the SD archive (build.yml) and as a small overlay for the A/B on
the Switch (fex-variant.yml). Nothing here builds FEX, and nothing here
publishes it either -- a second builder is how the A/B ends up comparing two
builds that are not the ones anyone chose.

That is not hypothetical: while the startup crash was bisected, this
repository built the variant itself, with the tuning switched off, and the
release kept the byte-twin. The modules a card runs have one author.
"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[2]
variant = (root / '.github/workflows/fex-variant.yml').read_text()
build = (root / '.github/workflows/build.yml').read_text()
doc = (root / 'horizon-wine/fex/variants-build.md').read_text()

failures = []


def want(condition, message):
    if not condition:
        failures.append(message)


def fetches(workflow):
    """Does this workflow take the fex-opt release from both places the FEX work publishes?"""
    owners = re.search(r'for owner in ([^\n;]+)', workflow)
    return (bool(re.search(r'"https://github\.com/\$owner/releases/download/fex-opt/', workflow)) and
            bool(owners) and 'berlint-hub/FEX' in owners.group(1) and
            'berlint-hub/autorun' in owners.group(1))


# The overlay workflow only takes, verifies and packs.
want(fetches(variant), 'fex-variant.yml does not take the fex-opt release from the FEX work')
want('sha256sum -c SHA256SUMS' in variant, 'fex-variant.yml takes the modules without checking them')
want('build-fex.sh' not in variant and 'Build the tuned FEX' not in variant,
     'fex-variant.yml builds FEX; that is the FEX work\'s')
want('WINE_NX_FEX_TUNE_TEGRA_X1' not in variant and 'WINE_NX_FEX_SUFFIX' not in variant,
     'fex-variant.yml sets the FEX build inputs; the tuning is not decided here')
want(not re.search(r'gh release (create|upload)', variant),
     'fex-variant.yml publishes a release; the FEX work publishes its own')

# The archive takes the same release, the same way, with no build fallback.
want(fetches(build), 'build.yml does not take the fex-opt release from the FEX work')
want('sha256sum -c SHA256SUMS' in build, 'build.yml takes the modules without checking them')
want('WINE_NX_FEX_TUNE_TEGRA_X1' not in build and 'WINE_NX_FEX_SUFFIX' not in build,
     'build.yml builds a FEX variant of its own')

# The handover: who builds, who packs, and what a proposal has to contain.
want('arena/21bcd47b-autorun' in doc, 'variants-build.md does not say which work owns the FEX build')
want('fex-opt' in doc and 'release' in doc, 'variants-build.md does not say the modules arrive as a release')
want('Návrh optimalizace' in doc or 'A proposal' in doc,
     'variants-build.md has no shape for handing an optimisation to the FEX work')

if failures:
    for message in failures:
        print(f'fex payload source: {message}', file=sys.stderr)
    sys.exit(1)
print('FEX payload source: taken from the FEX work\'s release, checked, never built here')
