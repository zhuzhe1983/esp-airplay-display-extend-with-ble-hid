#!/usr/bin/env python3
"""Create the source download and update the static delivery site from recorded evidence."""
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.1.0"
SKIP = {"managed_components", "__pycache__", ".git", ".build", ".tools", "build"}
ROOT_FILES = ("README.md", "LICENSE", "THIRD_PARTY.md", ".gitignore")

def included(path):
    rel = path.relative_to(ROOT)
    return not any(p in SKIP or p.startswith("build-") for p in rel.parts) and path.suffix not in {".pyc", ".o", ".a", ".log", ".elf", ".map"} and path.name not in {"sdkconfig", "sdkconfig.old"}

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    report = json.loads((ROOT / "docs/verification.json").read_text())
    destination = ROOT / "dist/downloads"
    destination.mkdir(parents=True, exist_ok=True)
    files = [ROOT / f for f in ROOT_FILES]
    for directory in ("firmware", "tests", "tools", "docs", ".github"):
        base = ROOT / directory
        if base.exists():
            files.extend(p for p in base.rglob("*") if p.is_file() and included(p))
    files = sorted(set(files), key=lambda p: p.relative_to(ROOT).as_posix())
    manifest = {p.relative_to(ROOT).as_posix(): digest(p) for p in files}
    archive = destination / f"p4-native-display-source-v{VERSION}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for path in files:
            rel = path.relative_to(ROOT).as_posix()
            entry = zipfile.ZipInfo("p4-native-display/" + rel, date_time=(2026, 11, 24, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = (0o100755 if path.stat().st_mode & 0o111 else 0o100644) << 16
            z.writestr(entry, path.read_bytes())
        z.writestr("p4-native-display/SOURCE_SHA256.json", json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    with zipfile.ZipFile(archive) as z:
        if z.testzip() is not None:
            raise RuntimeError("ZIP integrity failed")
    sha = digest(archive)
    (destination / "SHA256SUMS.txt").write_text(f"{sha}  {archive.name}\n")
    docs = ROOT / "dist/docs"
    docs.mkdir(exist_ok=True)
    for path in (ROOT / "docs").glob("*.md"):
        shutil.copy2(path, docs / path.name)
    for name in ("README.md", "THIRD_PARTY.md"):
        shutil.copy2(ROOT / name, docs / name)
    report.update(version=VERSION, source={"filename":archive.name,"bytes":archive.stat().st_size,"sha256":sha,"files":len(files)})
    (ROOT / "dist/release.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n")
    print(json.dumps({"archive":str(archive),"files":len(files),"bytes":archive.stat().st_size,"sha256":sha},ensure_ascii=False))

if __name__ == "__main__":
    main()
