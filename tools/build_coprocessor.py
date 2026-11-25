#!/usr/bin/env python3
"""Build the matching ESP-Hosted Wi-Fi + BLE controller firmware; never flash it."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
COMMIT = "471900d45c3b9208da10a01beb200b97f15a161a"
URL = "https://github.com/espressif/esp-hosted-mcu.git"

def run(args, cwd=None):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=cwd, check=True)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("board", choices=("metalio_claw4", "m5stack_tab5"))
    p.add_argument("--source", type=Path, help="Existing clean checkout at the pinned ESP-Hosted commit")
    p.add_argument("--menuconfig", action="store_true")
    args = p.parse_args()
    if not os.environ.get("IDF_PATH") or not shutil.which("idf.py"):
        p.error("Source ESP-IDF v5.5.4 export.sh first.")
    source = (args.source or ROOT / ".tools" / "esp-hosted-mcu").resolve()
    if not source.exists():
        source.parent.mkdir(parents=True, exist_ok=True)
        run(["git", "clone", "--depth", "1", "--branch", "v2.12.12", URL, source])
    actual = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    if actual != COMMIT:
        p.error(f"ESP-Hosted revision is {actual}; expected {COMMIT}. Existing checkout was not changed.")
    changed = subprocess.check_output(["git", "diff", "--name-only", "HEAD"], cwd=source, text=True).strip()
    if changed:
        p.error("ESP-Hosted source has tracked modifications; use a clean pinned checkout for a reproducible build.")
    target = "esp32c5" if args.board == "metalio_claw4" else "esp32c6"
    build = ROOT / ".build" / f"coprocessor-{args.board}"
    slave = source / "slave"
    lock_template = ROOT / "firmware/coprocessor/locks" / f"{target}.lock"
    if not lock_template.is_file():
        p.error(f"Missing pinned dependency lock: {lock_template}")
    # The upstream manifest uses one SDK-owned local console component. Keep
    # the distributable lock portable and materialize its path for this SDK.
    lock_text = lock_template.read_text().replace("${IDF_PATH}", Path(os.environ["IDF_PATH"]).resolve().as_posix())
    (slave / "dependencies.lock").write_text(lock_text)
    defaults = [slave / "sdkconfig.defaults", slave / f"sdkconfig.defaults.{target}", ROOT / "firmware/coprocessor/boards" / f"{args.board}.defaults"]
    for path in defaults:
        if not path.is_file():
            p.error(f"Missing defaults: {path}")
    build.mkdir(parents=True, exist_ok=True)
    run(["idf.py", "-B", build, "-D", f"IDF_TARGET={target}", "-D", f"SDKCONFIG={build / 'sdkconfig'}", "-D", "SDKCONFIG_DEFAULTS=" + ";".join(map(str, defaults)), "menuconfig" if args.menuconfig else "build"], cwd=slave)
    resolved_lock = slave / "dependencies.lock"
    if resolved_lock.exists():
        shutil.copy2(resolved_lock, build / "dependencies.lock")
    shutil.copy2(defaults[-1], build / "board.defaults")
    print("Build complete. No hardware was modified. Read firmware/coprocessor/README.md before choosing a board-specific recovery/flash path.")

if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
