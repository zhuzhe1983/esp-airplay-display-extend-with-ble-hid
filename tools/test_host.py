#!/usr/bin/env python3
"""Run real codec, protocol parser and input-report tests without ESP hardware."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / ".build" / "decoder"

def run(*args):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=ROOT, check=True)

try:
    run("cmake", "-S", ROOT / "tests/decoder", "-B", BUILD, "-DCMAKE_BUILD_TYPE=Release")
    run("cmake", "--build", BUILD, "-j", "4")
    run("ctest", "--test-dir", BUILD, "--output-on-failure")
    run("bash", ROOT / "firmware/components/airplay_receiver/tests/run_host_tests.sh")
    run("bash", ROOT / "tests/input/run.sh")
    run("bash", ROOT / "tests/runtime/run.sh")
except subprocess.CalledProcessError as error:
    sys.exit(error.returncode)
