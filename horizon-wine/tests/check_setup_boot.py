from pathlib import Path
import os
import subprocess
import tempfile
import test_support

root = Path(__file__).resolve().parents[1]
test_support.need_header('openssl/sha.h', 'setup_boot.c hashes the payloads with OpenSSL')
with tempfile.TemporaryDirectory(prefix='autorun-boot-tests-') as directory:
    executable = Path(directory) / 'setup-boot-test'
    subprocess.run([
        test_support.compiler(), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        str(root / 'tests/setup_boot.c'), '-lcrypto', '-o', str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
