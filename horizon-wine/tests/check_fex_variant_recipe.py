#!/usr/bin/env python3
"""The FEX variant recipe: one tuned build, and a control that is not published.

What an `-opt` module is depends on two files: the DLL repository's recipe,
kept here as `horizon-wine/fex/dlls-fex-variants.patch` until it lands, and
the FEX variant workflow that builds, packages and publishes it. Both have to
ask for the Cortex-A57 tuning, and the `fex-opt` release -- the thing the main
Build fetches into every SD archive -- must never be the byte-twin control.

That is not hypothetical. The first tuned variant died at the first guest init
and the tuning was switched off for a bisect; the cause was ntdll pairing the
CPU module by its exact name, which the runtime now pairs by stem, and the
byte-twin died the same way. A workflow that silently builds the stock flags
again, or publishes the control where the cards look for the tuned modules,
would put the A/B back to comparing a build with itself.
"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[2]
workflow = (root / '.github/workflows/fex-variant.yml').read_text()
recipe = (root / 'horizon-wine/fex/dlls-fex-variants.patch').read_text()
doc = (root / 'horizon-wine/fex/variants-build.md').read_text()

failures = []


def want(condition, message):
    if not condition:
        failures.append(message)


# The workflow: the dispatch input, both sides of it, and the publish gate.
want(re.search(r'tune:.*?options:\s*\[\s*"on"\s*,\s*"off"\s*\]', workflow, re.S),
     'the FEX variant workflow has no tune input with the on/off choice')
want('default: "on"' in workflow, 'the FEX variant workflow does not default to the tuned build')
want('github.event.inputs.tune' in workflow, 'the FEX variant workflow ignores its tune input')
want('export WINE_NX_FEX_TUNE_TEGRA_X1=ON' in workflow,
     'the FEX variant workflow builds without the Cortex-A57 tuning')
want('export WINE_NX_FEX_TUNE_TEGRA_X1=OFF' in workflow,
     'the FEX variant workflow cannot build the byte-twin control')
want(re.search(r'name: Publish the fex-opt release\s*\n\s*if: .*tune.*!= *\'off\'', workflow),
     'the fex-opt release is not gated on the build being the tuned one')

# The DLL repository's recipe, kept as a patch: the fex-opt entry, the build
# that records the tuning, and the payload validation that reads it back.
want("'WINE_NX_FEX_SUFFIX': '-opt'" in recipe,
     'the recipe does not build the variant under the -opt names')
want("'WINE_NX_FEX_TUNE_TEGRA_X1': 'ON'" in recipe,
     'the recipe builds the variant with the stock flags')
want('--tune "$tune_cpu"' in recipe, 'build-fex.sh does not record what the payload was tuned for')
want("'tune': tuned_cpu(tune)" in recipe, 'the payload manifest does not record the tuning')
want("manifest.get('tune') != tuned_cpu(tune)" in recipe,
     'a payload tuned differently from the recipe would be accepted')

# The workflow builds the release and build.sh --dlls builds the package's own
# copy: they have to agree, or the card and the release are two different A/Bs.
want('WINE_NX_FEX_TUNE_TEGRA_X1=ON' in workflow and "': 'ON'" in recipe,
     'the workflow and the recipe disagree on whether the variant is tuned')

# The control exists to be documented; it is no use if nobody finds it.
want('WINE_NX_FEX_TUNE_TEGRA_X1' in doc and 'tune: off' in doc,
     'variants-build.md does not say how to build the control')

if failures:
    for message in failures:
        print(f'fex variant recipe: {message}', file=sys.stderr)
    sys.exit(1)
print('FEX variant recipe: tuned by default, control buildable, release gated')
