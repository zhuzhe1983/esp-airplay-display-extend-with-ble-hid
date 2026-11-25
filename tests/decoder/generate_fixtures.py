#!/usr/bin/env python3
"""Reproducible local fixtures, generated images only; requires FFmpeg + libx264.

The checked-in files suffice to run tests without FFmpeg or a network download.
Reference samples come from FFmpeg's decoder and RGB converter, not our wrapper.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parent / "fixtures"
ROOT.mkdir(exist_ok=True)

def run(*args):
    return subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", *args],
                          check=True, stdout=subprocess.PIPE).stdout

def annexb_nals(stream):
    starts = list(re.finditer(b"\x00\x00(?:\x00)?\x01", stream))
    for i, start in enumerate(starts):
        end = starts[i+1].start() if i+1 < len(starts) else len(stream)
        yield stream[start.end()] & 31, stream[start.end():end], stream[start.start():end]

def pps_cabac(nal):
    rbsp = re.sub(b"\x00\x00\x03", b"\x00\x00", nal[1:])
    bits = "".join(f"{byte:08b}" for byte in rbsp)
    pos = 0
    for _ in range(2):  # pic_parameter_set_id and seq_parameter_set_id
        zeros = 0
        while bits[pos] == "0":
            zeros += 1
            pos += 1
        pos += 1 + zeros
    return bits[pos] == "1"

metadata = {"generator": "FFmpeg lavfi testsrc2 + libx264", "frames": 8,
            "source": [160, 96], "panel": [80, 80],
            "sample_x": list(range(5, 80, 10)),
            "sample_y_in_view": list(range(5, 48, 10)), "streams": {}}
for profile in ("baseline", "main", "high"):
    path = ROOT / f"{profile}.h264"
    cabac = 0 if profile == "baseline" else 1
    bframes = 0 if profile == "baseline" else 2
    matrix = "bt709" if profile == "high" else "smpte170m"
    params = (f"aud=1:repeat-headers=1:scenecut=0:keyint=8:ref=2:"
              f"cabac={cabac}:bframes={bframes}:b-pyramid=0:slices=2:"
              f"8x8dct={int(profile == 'high')}")
    run("-f", "lavfi", "-i", "testsrc2=size=160x96:rate=10", "-frames:v", "8",
        "-c:v", "libx264", "-profile:v", profile, "-preset", "medium", "-crf", "20",
        "-pix_fmt", "yuv420p", "-color_range", "tv", "-colorspace", matrix,
        "-color_primaries", matrix, "-color_trc", matrix,
        "-x264-params", params, "-f", "h264", str(path))
    rgb = run("-i", str(path), "-vf", "scale=iw:ih:flags=neighbor+accurate_rnd",
              "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1")
    assert len(rgb) == 8 * 160 * 96 * 3
    samples = bytearray()
    for frame in range(8):
        for y in metadata["sample_y_in_view"]:
            for x in metadata["sample_x"]:
                offset = (frame * 160 * 96 + (y * 2) * 160 + x * 2) * 3
                samples += rgb[offset:offset+3]
    (ROOT / f"{profile}.rgb.samples").write_bytes(samples)
    probe = subprocess.check_output(["ffprobe", "-v", "error", "-show_entries",
        "stream=profile,width,height,has_b_frames", "-of", "json", str(path)])
    actual_cabac = [pps_cabac(nal) for kind, nal, _ in annexb_nals(path.read_bytes()) if kind == 8]
    assert actual_cabac and all(value == bool(cabac) for value in actual_cabac)
    metadata["streams"][profile] = {
        **json.loads(probe)["streams"][0], "cabac_requested": bool(cabac),
        "cabac_verified_from_pps": actual_cabac[0],
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "x264_params": params}

# Tiny parameter-only fixtures exercise the 1080->1088 macroblock padding limit
# and the reference-surface cap without retaining large video test assets.
for name, size, refs in (("1080p", "1920x1080", 2), ("many_refs", "160x96", 5)):
    stream = run("-f", "lavfi", "-i", f"color=black:size={size}:rate=1",
                 "-frames:v", "1", "-c:v", "libx264", "-profile:v", "high",
                 "-pix_fmt", "yuv420p", "-x264-params",
                 f"aud=1:repeat-headers=1:ref={refs}:bframes=0:cabac=1:8x8dct=1",
                 "-f", "h264", "pipe:1")
    parameters = bytearray()
    for kind, _, unit in annexb_nals(stream):
        if kind in (7, 8):
            parameters += unit
    assert parameters
    (ROOT / f"{name}.params.h264").write_bytes(parameters)
(ROOT / "manifest.json").write_text(json.dumps(metadata, indent=2) + "\n")
print(json.dumps(metadata, indent=2))
