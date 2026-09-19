#!/usr/bin/env python3
"""Reject proprietary binary artifacts (Samsung blobs / dongle firmware) in diffs.

Used by the pre-commit hook (tools/githooks/pre-commit) and CI:

    python tools/check_proprietary.py check-staged
    python tools/check_proprietary.py check-path <file> [<file>...]

A file is banned when its name matches a proprietary artifact signature
(Samsung SSC blobs, generic ``.so``, Realtek/CSR firmware). DELETIONS are
never rejected, so removing the currently-bundled blob stays possible.

Exit code 0 = clean, 1 = banned files found (name):path pairs on stderr.
"""

import subprocess
import sys

BANNED_SUFFIXES = (".so", ".fw", ".bin", ".psr")
BANNED_PREFIXES = ("rtl8761", "rtl8763", "rtl87", "csr")
BANNED_NAMES = (
    "libscalable_encoder",
    "libscalable_decoder",
    "lib_bt_bundle",
)


def is_banned(rel_path):
    base = rel_path.replace("\\", "/").rsplit("/", 1)[-1].lower()
    if base.startswith(BANNED_PREFIXES):
        return True
    if base == BANNED_NAMES or base.startswith(BANNED_NAMES):
        return True
    return base.endswith(BANNED_SUFFIXES)


def git(*args):
    proc = subprocess.run(["git", *args], capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit("git %s failed: %s" % (args[0], proc.stderr.strip()))
    return proc


def cmd_check_staged():
    proc = git(
        "diff", "--staged", "--name-only", "--no-renames",
        "--diff-filter=ACMRTUB",
    )
    paths = [line for line in proc.stdout.splitlines() if line.strip()]
    return check_paths(paths, "staged changes")


def cmd_check_path(paths):
    return check_paths(paths, "given paths")


def check_paths(paths, label):
    bad = []
    for p in paths:
        if is_banned(p.replace("/", "\\")):
            bad.append(p)
    if bad:
        sys.stderr.write(
            "BANNED proprietary binary artifact(s) in %s:\n" % label
        )
        for p in sorted(bad):
            sys.stderr.write("  %s\n" % p)
        sys.stderr.write(
            "These files are proprietary/gettable binaries (Samsung SSC blobs,\n"
            "*.so, dongle firmware). Additions are rejected; deletions are fine.\n"
            "See docs/dev/audit-2026.md and CONTRIBUTING.md.\n"
        )
        return 1
    sys.stdout.write("proprietary check: clean (%d path(s))\n" % len(paths))
    return 0


def main(argv):
    if argv[:1] == ["check-staged"]:
        return cmd_check_staged()
    if argv[:1] == ["check-path"] and len(argv) > 1:
        return cmd_check_path(argv[1:])
    sys.stderr.write(
        "usage: check_proprietary.py check-staged | check-path <file>...\n"
    )
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))