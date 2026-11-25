#!/usr/bin/env python3
"""Build one P4 board using its own build directory and sdkconfig."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("board", choices=("metalio_claw4", "m5stack_tab5"))
    p.add_argument("action", choices=("build", "menuconfig", "flash", "monitor", "size", "fullclean"), default="build", nargs="?")
    p.add_argument("--port", help="Serial port for flash/monitor")
    args = p.parse_args()
    if not os.environ.get("IDF_PATH") or not shutil.which("idf.py"):
        p.error("Install ESP-IDF v5.5.4 for esp32p4, then source its export.sh first.")
    command = ["idf.py", "-B", f"build-{args.board}", "-D", f"BOARD={args.board}"]
    if args.port:
        command += ["-p", args.port]
    command.append(args.action)
    return subprocess.call(command, cwd=ROOT / "firmware")

if __name__ == "__main__":
    sys.exit(main())
