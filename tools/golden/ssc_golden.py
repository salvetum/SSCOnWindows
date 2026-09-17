#!/usr/bin/env python3
"""SSC golden-file regression tool.

Frozen-reference test for the WSL2/qemu SSC blob daemon (TCP :20248, see
AGENTS.md "SSC encoder architecture").

Wire protocol (matches app/src/ssc_encoder.cpp):
  client -> daemon: uint32 frame_samples (LE), then frame_samples*ch*4 bytes
                    of int32 PCM (24-bit, 2^29 scale)
  daemon -> client: int32 ret, then ret bytes of the encoded SSC frame

Usage:
  python ssc_golden.py gen  --bitrate 229000 --frames 16 --out 229k.golden
  python ssc_golden.py check --golden 229k.golden
  python ssc_golden.py check --all
"""

import argparse
import math
import os
import random
import socket
import struct
import subprocess
import sys

FRAME_SAMPLES = 864
CHANNELS = 2
RATE = 48000
MAX_ENCODE = 4096
DAEMON_PORT = 20248
PCM_SCALE = 1 << 29
MAGIC = b"SSCG"
VERSION = 1
DEFAULT_SEED = 0x5A5C_2026


def wsl_cmd():
    parts = ["wsl"]
    distro = os.environ.get("SSC_WSL_DISTRO")
    if distro:
        parts += ["-d", distro]
    return parts


def wsl_ip():
    out = subprocess.run(
        wsl_cmd() + ["--", "bash", "-lc", "hostname -I"],
        capture_output=True, text=True, timeout=30,
    )
    if out.returncode != 0:
        print("could not resolve WSL2 IP", file=sys.stderr)
        sys.exit(1)
    toks = out.stdout.split()
    if not toks:
        print("WSL2 IP empty (VM not ready?)", file=sys.stderr)
        sys.exit(1)
    return toks[0]


def restart_daemon(bitrate, rate=RATE, ch=CHANNELS):
    """Kill + restart sscblobd with the given codec config."""
    script = os.environ.get(
        "SSC_DAEMON_SCRIPT", "/home/kaan5/ssc/bin/start_sscblobd")
    res = subprocess.run(
        wsl_cmd() + ["--", "bash", "-lc",
                     f"{script} {DAEMON_PORT} {rate} {ch} {bitrate}"],
        capture_output=True, text=True, timeout=120,
    )
    if "sscblobd started" not in res.stdout:
        print("daemon failed:", res.stdout, res.stderr, file=sys.stderr)
        sys.exit(1)


def connect():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    s.settimeout(10.0)
    s.connect((wsl_ip(), DAEMON_PORT))
    return s


def recv_exact(sock, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("daemon closed connection")
        buf.extend(chunk)
    return bytes(buf)


def gen_frame_pcm(seed, frame_index, rate=RATE):
    """Deterministic int32 PCM frame (864 x 2ch, 2^29 scale)."""
    r = random.Random(seed + frame_index * 0x10000)
    vals = []
    for i in range(FRAME_SAMPLES * CHANNELS):
        tone = PCM_SCALE * 0.5 * math.sin(
            2.0 * math.pi * (440.0 + 110.0 * math.sin(frame_index)) * (i % FRAME_SAMPLES) / rate
        )
        dither = r.uniform(-0.06, 0.06) * PCM_SCALE
        vals.append(int(tone + dither))
    return struct.pack("<%di" % (FRAME_SAMPLES * CHANNELS), *vals)


def encode_one(sock, pcm):
    sock.sendall(struct.pack("<I", FRAME_SAMPLES))
    sock.sendall(pcm)
    (ret,) = struct.unpack("<i", recv_exact(sock, 4))
    if ret <= 0 or ret > MAX_ENCODE:
        raise RuntimeError(f"bad ret={ret}")
    payload = recv_exact(sock, ret)
    return ret, payload


def gen_golden(bitrate, frames=16, out_path=None, seed=DEFAULT_SEED, rate=RATE):
    restart_daemon(bitrate, rate=rate)
    sock = connect()
    payloads = []
    try:
        for f in range(frames):
            ret, payload = encode_one(sock, gen_frame_pcm(seed, f, rate))
            payloads.append(payload)
    finally:
        sock.close()

    if out_path is None:
        out_path = f"{bitrate // 1000}k.golden"
    header = struct.pack("<4sHHIIIII",
                         MAGIC, VERSION, 0,
                         rate, CHANNELS, bitrate, frames, seed)
    blob = header + b"".join(struct.pack("<I", len(p)) + p for p in payloads)
    with open(out_path, "wb") as fh:
        fh.write(blob)
    sizes = [len(p) for p in payloads]
    print(f"golden written: {out_path}  bitrate={bitrate} rate={rate} frames={frames} "
          f"size={min(sizes)}..{max(sizes)} bytes/frame")


def load_golden(path):
    with open(path, "rb") as fh:
        blob = fh.read()
    magic, ver, _rsv, rate, ch, br, frames, seed = struct.unpack_from(
        "<4sHHIIIII", blob, 0)
    if magic != MAGIC or ver != VERSION:
        raise RuntimeError(f"{path}: bad header magic/version")
    off = struct.calcsize("<4sHHIIIII")
    payloads = []
    for _ in range(frames):
        (n,) = struct.unpack_from("<I", blob, off)
        off += 4
        payloads.append(blob[off:off + n])
        off += n
    return rate, ch, br, frames, seed, payloads


def check_golden(path):
    rate, ch, br, frames, seed, expect = load_golden(path)
    restart_daemon(br, rate=rate, ch=ch)
    sock = connect()
    n_pass = 0
    try:
        for f in range(frames):
            ret, payload = encode_one(sock, gen_frame_pcm(seed, f, rate))
            if payload != expect[f]:
                n = next((i for i, (a, b) in enumerate(zip(payload, expect[f]))
                          if a != b), None)
                print(f"  frame {f}: MISMATCH ret={ret} expect_len={len(expect[f])} "
                      f"first_diff_byte={n}", file=sys.stderr)
                return False
            n_pass += 1
    finally:
        sock.close()
    print(f"  {path}: PASS  ({frames}/{frames} frames byte-identical, "
          f"bitrate={br}, {rate}Hz {ch}ch)")
    return True


def main():
    ap = argparse.ArgumentParser(description="SSC golden-file regression tool")
    sub = ap.add_subparsers(dest="cmd", required=True)

    g = sub.add_parser("gen", help="freeze current daemon output as golden")
    g.add_argument("--bitrate", type=int, required=True)
    g.add_argument("--frames", type=int, default=16)
    g.add_argument("--out", default=None)
    g.add_argument("--seed", type=lambda s: int(s, 0), default=DEFAULT_SEED)
    g.add_argument("--rate", type=int, default=RATE)

    c = sub.add_parser("check", help="regenerate and byte-compare against golden")
    c.add_argument("--golden", default=None)
    c.add_argument("--all", action="store_true", help="check every *.golden here")

    args = ap.parse_args()

    if args.cmd == "gen":
        gen_golden(args.bitrate, args.frames, args.out, args.seed, args.rate)
    else:
        if args.all:
            here = os.path.dirname(os.path.abspath(__file__))
            paths = sorted(p for p in os.listdir(here) if p.endswith(".golden"))
            if not paths:
                print("no *.golden files found here", file=sys.stderr)
                sys.exit(1)
        elif args.golden:
            paths = [args.golden]
        else:
            ap.error("check requires --golden or --all")

        ok = True
        for p in paths:
            ok &= check_golden(p)
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()