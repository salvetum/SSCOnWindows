#!/usr/bin/env python3
"""Qiling smoke test - run the aarch64 SSC blob helper in Qiling on Windows.

Feeds golden PCM frames (same seed/order as tools/golden/ssc_golden.py) into
the helper's stdin via Qiling and byte-compares stdout against the frozen
golden payload. No WSL/qemu involved.

Usage:
  python smoke_test.py [golden_path]
    golden_path defaults to tools\\golden\\229k.golden
"""

import io
import os
import struct
import sys

from qiling import Qiling

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROOTFS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "rootfs")
HELPER = os.path.join(ROOTFS, "helper", "ssc_blob_helper")
GOLDEN_DIR = os.path.join(REPO, "tools", "golden")

sys.path.insert(0, GOLDEN_DIR)
from ssc_golden import FRAME_SAMPLES, gen_frame_pcm, load_golden  # noqa: E402


def feed_stream(pcm_frames):
    """Build the exact byte stream the helper reads from stdin (protokol)."""
    chunks = []
    for pcm in pcm_frames:
        chunks.append(struct.pack("<I", FRAME_SAMPLES))
        chunks.append(pcm)
    return b"".join(chunks)


def main():
    golden_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        GOLDEN_DIR, "229k.golden")
    rate, ch, br, frames, seed, expect = load_golden(golden_path)

    pcm_frames = [gen_frame_pcm(seed, f, rate) for f in range(frames)]
    stdin_bytes = feed_stream(pcm_frames)

    ql = Qiling(
        [HELPER, "/blob/libScalable_Encoder.so",
         str(rate), str(ch), "24", str(br)],
        rootfs=ROOTFS,
        env={"LD_LIBRARY_PATH": "/shims", "QILING_LOG": "DISABLED"},
        verbose=0,
    )
    ql.os.stdin = io.BytesIO(stdin_bytes)
    out = io.BytesIO()
    ql.os.stdout = out
    ql.os.stderr = io.BytesIO()

    from qiling_patches import install_lse_atomics
    install_lse_atomics(ql)

    ql.run()

    raw = out.getvalue()
    if len(raw) < 4:
        print(f"FAIL: no output, got {len(raw)} bytes")
        return 1
    ready = struct.unpack_from("<i", raw, 0)[0]
    body = raw[4:]
    if ready != 0:
        print(f"FAIL: helper not ready (ready={ready}), got {len(raw)} bytes")
        return 1

    expected_body = b"".join(
        struct.pack("<I", len(p)) + p for p in expect)
    if body == expected_body:
        total = sum(len(p) for p in expect)
        print(f"PASS: {os.path.basename(golden_path)} {frames}/{frames} frames "
              f"byte-identical ({total} payload bytes, rate={rate} ch={ch} br={br})")
        return 0

    print("FAIL: byte mismatch")
    n = next((i for i, (a, b) in enumerate(zip(body, expected_body))
              if a != b), min(len(body), len(expected_body)))
    print(f"  helper out len={len(body)} expected={len(expected_body)} first_diff={n}")
    return 1


if __name__ == "__main__":
    sys.exit(main())