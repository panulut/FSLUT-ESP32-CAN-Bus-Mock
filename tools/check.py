#!/usr/bin/env python3
"""Run PC tests without a connected ESP32. Requires a C++17 compiler (or Zig)."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', help='path to g++, clang++, or zig executable')
    args = parser.parse_args()
    build = ROOT / 'build'
    build.mkdir(exist_ok=True)
    subprocess.run([sys.executable, '-m', 'unittest', 'discover', '-s', 'tests',
                    '-p', 'test_tools.py', '-v'], cwd=ROOT, check=True)
    compiler = args.cxx or shutil.which('g++') or shutil.which('clang++') or shutil.which('c++')
    if not compiler:
        local = ROOT / '.tools/native/ziglang/zig.exe'
        if local.exists():
            compiler = str(local)
    if not compiler:
        raise SystemExit('C++ tests not run: install a C++17 compiler or pass --cxx PATH')
    command = [compiler]
    if Path(compiler).stem == 'zig':
        command.append('c++')
    binary = build / ('test_core.exe' if sys.platform == 'win32' else 'test_core')
    command += ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'include'),
                str(ROOT / 'tests/test_core.cpp'), '-o', str(binary)]
    subprocess.run(command, cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True)
    subprocess.run([sys.executable, str(ROOT / 'tools/demo.py'), '--cxx', compiler,
                    '--self-test'], cwd=ROOT, check=True)
    subprocess.run([sys.executable, str(ROOT / 'tools/test_demo_http.py'), '--spawn'],
                   cwd=ROOT, check=True)


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode)
