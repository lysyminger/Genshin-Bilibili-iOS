"""Run offline C/verifier/privacy regressions; optional local binary integration tests."""
import argparse
import ast
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / 'Genshin-Bilibili-iOS'
sys.path.insert(0, str(PROJECT))
from local_config import local_path


def run(command, cwd=ROOT):
    subprocess.run([str(part) for part in command], cwd=cwd, check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', type=Path)
    parser.add_argument('--integration', action='store_true')
    args = parser.parse_args()
    compiler = args.zig or local_path('zig_relative', '.tools/zig13/ziglang/zig.exe')
    if not compiler.is_file() and sys.platform != 'win32':
        compiler = compiler.with_suffix('')
    if not compiler.is_file():
        parser.error('Install Zig0.13.0 as documented, or pass --zig.')
    for path in [*PROJECT.glob('*.py'), *(ROOT / 'research').glob('*.py'), *(ROOT / 'tools').glob('*.py')]:
        ast.parse(path.read_text(encoding='utf-8'), filename=str(path.relative_to(ROOT)))
    output = PROJECT / 'build/tests'
    output.mkdir(parents=True, exist_ok=True)
    programs = sorted(PROJECT.glob('test_*.c'))
    for source in programs:
        binary = output / (source.stem + ('.exe' if sys.platform == 'win32' else ''))
        run([compiler, 'cc', '-O2', '-DGB_CATALOG_TEST_PROFILE=1', source, '-o', binary])
        run([binary])
    scanner = output / ('test_android_scanner' + ('.exe' if sys.platform == 'win32' else ''))
    run([compiler, 'cc', '-O2', '-DSCAN_TEST=1', ROOT / 'research/scan_android_dispatch.c', '-o', scanner])
    run([scanner])
    run([sys.executable, '-m', 'unittest', '-v', 'test_catalog_proof'], cwd=PROJECT)
    run([sys.executable, '-m', 'unittest', '-v', 'test_publication'], cwd=ROOT / 'tools')
    if args.integration:
        run([sys.executable, '-m', 'unittest', '-v', 'test_bili_candidate', 'test_signing_header', 'test_url_writer_patch'], cwd=PROJECT)
    print(f'Passed: {len(programs)} C bridge programs, scanner regression, verifier and publication checks.', flush=True)
