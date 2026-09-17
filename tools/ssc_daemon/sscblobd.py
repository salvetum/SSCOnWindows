#!/usr/bin/env python3
"""sscblobd - Windows-native SSC encode daemon (Qiling-based, no WSL2).

Drop-in replacement for the WSL2 sscblobd.c daemon. Instead of spawning
qemu-aarch64 per client, it runs the aarch64 Samsung SSC blob helper inside
Qiling (Unicorn) in-process, bridging stdin/stdout over real OS pipes so the
helper's `read_full()`/`write_full()` are satisfied frame-by-frame without a
process per frame.

Wire protocol is identical to the WSL daemon (matches app/src/ssc_encoder.cpp):
  client -> daemon: uint32 frame_samples (LE), then frame_samples*ch*4 bytes
                    of int32 PCM (24-bit, 2^29 scale)
  daemon -> client: int32 ret, then ret bytes of the encoded SSC frame

CLI is the same shape as the C daemon:
  sscblobd.py <port> <sample-rate> <channels> <bitrate> [helper-path-prefix]

Extra: `--test` runs the frozen golden regression against itself (6 profiles,
restarting a daemon subprocess per profile, byte-comparing every golden).
"""

import argparse
import io
import os
import socket
import struct
import subprocess
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from qiling import Qiling  # noqa: E402
from qiling_patches import install_lse_atomics, install_rseq  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOTFS = os.path.join(HERE, "rootfs")
HELPER = os.path.join(ROOTFS, "helper", "ssc_blob_helper")
BLOB = "/blob/libScalable_Encoder.so"

MAX_FRAME_SAMPLES = 16384
MAX_ENCODE_BYTES = 4096


class HelperBridge:
    """One persistent Qiling helper instance bridged over OS pipes."""

    def __init__(self, sample_rate, channels, bitrate):
        self.q = None
        self._t = None
        self._in_w = None
        self._out_r = None
        self._ready = False

        in_r, in_w = os.pipe()
        out_r, out_w = os.pipe()
        self._in_w = os.fdopen(in_w, "wb", buffering=0)
        self._out_r = os.fdopen(out_r, "rb", buffering=0)
        in_rd = os.fdopen(in_r, "rb", buffering=0)
        out_wr = os.fdopen(out_w, "wb", buffering=0)

        self.q = Qiling(
            [HELPER, BLOB, str(sample_rate), str(channels), "24", str(bitrate)],
            rootfs=ROOTFS,
            env={"LD_LIBRARY_PATH": "/shims"},
            verbose=0,
        )
        self.q.os.stdin = in_rd
        self.q.os.stdout = out_wr
        self.q.os.stderr = getattr(sys.stderr, "buffer", None) or io.BytesIO()
        install_rseq(self.q)
        install_lse_atomics(self.q)

        self._t = threading.Thread(target=self._run, daemon=True)
        self._t.start()

        ready = self._read_exact(4)
        ready = struct.unpack("<i", ready)[0]
        if ready != 0:
            raise RuntimeError(f"helper not ready (ready={ready})")
        self._ready = True

    def _run(self):
        try:
            self.q.run()
        except Exception as e:  # surface via stderr, thread must not die silently
            print(f"sscblobd: qiling run error: {type(e).__name__}: {e}",
                  file=sys.stderr, flush=True)

    def _read_exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self._out_r.read(n - len(buf))
            if not chunk:
                raise ConnectionError("helper closed stdout")
            buf += chunk
        return buf

    def encode(self, pcm):
        """Encode one frame (bytes -> payload bytes). Pipe-write is framed by
        the helper protocol, so a single write of frame_samples+PCM is atomic."""
        self._in_w.write(pcm)  # caller already prefixed frame_samples
        ret = struct.unpack("<i", self._read_exact(4))[0]
        if ret <= 0 or ret > MAX_ENCODE_BYTES:
            raise RuntimeError(f"bad ret={ret}")
        return self._read_exact(ret)

    def close(self):
        # Closing read end of the child's stdin makes read_full() see EOF ->
        # helper loop breaks and Qiling exits cleanly.
        if self._in_w is not None:
            try:
                self._in_w.close()
            except OSError:
                pass
            self._in_w = None
        if self._t is not None:
            self._t.join(timeout=5.0)
        if self._out_r is not None:
            try:
                self._out_r.close()
            except OSError:
                pass
            self._out_r = None


def run_daemon(port, sample_rate, channels, bitrate):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))
    srv.listen(4)
    print(f"sscblobd(py): listening on 127.0.0.1:{port} (rate={sample_rate} "
          f"ch={channels} br={bitrate} helper={HELPER})", flush=True)

    while True:
        conn, _ = srv.accept()
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        print("sscblobd(py): client connected", flush=True)
        try:
            service_client(conn, sample_rate, channels, bitrate)
        except Exception as e:
            print(f"sscblobd(py): client error: {type(e).__name__}: {e}",
                  file=sys.stderr, flush=True)
        finally:
            conn.close()
            print("sscblobd(py): client disconnected", flush=True)


def service_client(conn, sample_rate, channels, bitrate):
    bridge = HelperBridge(sample_rate, channels, bitrate)
    try:
        while True:
            head = recv_exact(conn, 4)
            if head is None:
                break
            (frame_samples,) = struct.unpack("<I", head)
            if frame_samples == 0 or frame_samples > MAX_FRAME_SAMPLES:
                print(f"sscblobd(py): bad frame_samples={frame_samples}",
                      file=sys.stderr)
                break
            pcm_bytes = frame_samples * channels * 4
            pcm = recv_exact(conn, pcm_bytes)
            if pcm is None:
                break
            payload = bridge.encode(head + pcm)
            conn.sendall(struct.pack("<i", len(payload)) + payload)
    finally:
        bridge.close()


def recv_exact(conn, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            return None
        buf.extend(chunk)
    return bytes(buf)


# ---------------------------------------------------------------------------
# Golden regression (wraps tools/golden/ssc_golden.py's helpers)
# ---------------------------------------------------------------------------

def run_test(frames=16):
    sys.path.insert(0, os.path.join(HERE, "..", "golden"))
    import ssc_golden as g

    here = os.path.join(HERE, "..", "golden")
    paths = sorted(p for p in os.listdir(here) if p.endswith(".golden"))
    ok = True
    for p in paths:
        rate, ch, br, gframes, seed, expect = g.load_golden(os.path.join(here, p))
        proc = subprocess.Popen(
            [sys.executable, os.path.abspath(__file__), "20248",
             str(rate), str(ch), str(br)],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            sock.settimeout(10.0)
            sock.connect(("127.0.0.1", 20248))
            n_pass = 0
            for f in range(frames):
                ret, payload = g.encode_one(sock, g.gen_frame_pcm(seed, f, rate))
                if payload != expect[f]:
                    n = next((i for i, (a, b) in enumerate(zip(payload, expect[f]))
                              if a != b), None)
                    print(f"  {p} frame {f}: MISMATCH ret={ret} "
                          f"expect_len={len(expect[f])} first_diff_byte={n}",
                          file=sys.stderr)
                    ok = False
                    break
                n_pass += 1
            sock.close()
            if n_pass == frames:
                print(f"  {p}: PASS ({frames}/{frames} frames byte-identical, "
                      f"bitrate={br}, {rate}Hz {ch}ch)")
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
    print("sscblobd(py): golden regression", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "--test":
        sys.exit(run_test())
    if len(sys.argv) < 5:
        print("usage: %s <port> <sample-rate> <channels> <bitrate> [--test]"
              % sys.argv[0], file=sys.stderr)
        sys.exit(2)
    port = int(sys.argv[1])
    rate = int(sys.argv[2])
    ch = int(sys.argv[3])
    br = int(sys.argv[4])
    run_daemon(port, rate, ch, br)


if __name__ == "__main__":
    main()