#!/usr/bin/env python3
"""
ui_style_check.py - rule checks for the design consistency plan (Faz D).

Runs the checks collected in docs/dev/PLAN_DESIGN_CONSISTENCY.md, Faz D
("Referans ve doğrulama"):

  cli    - piped (non-TTY) CLI output and --no-color must never contain ANSI
           escapes (console_style.h only paints on a real console).
  xaml   - winui3/*.xaml must not hand-place inline Foreground/FontSize/FontFamily
           literals; colors/typography come from App.xaml tokens/styles.
  setup  - setup.ps1 Stage/Die must speak the shared tag language
           (INFO / OK / WARN / ERROR / DATA).

Usage:
  python tools\\ui_style_check.py check --all --exe <path-to-ssconwindows.exe>
  python tools\\ui_style_check.py cli    --exe <path>
  python tools\\ui_style_check.py xaml
  python tools\\ui_style_check.py setup

Exit code 0 = PASS (each reported check succeeded).
"""

import argparse
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ---------------------------------------------------------------------------
# check: cli
# ---------------------------------------------------------------------------

def _run_exe(exe, extra):
    out = subprocess.run(
        [exe, "--cli", "-h", *extra],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        timeout=60,
    )
    return out.stdout + out.stderr

def check_cli(exe):
    """Piped output must be plain; --no-color must be honored."""
    if not os.path.isfile(exe):
        return ["CLI not found at %s (build it first)" % exe]

    problems = []
    for label, extra in (("piped (no flags)", ()), ("--no-color", ("--no-color",))):
        data = _run_exe(exe, extra)
        if b"\x1b" in data:
            problems.append("%s: ANSI escape found in piped output" % label)
    if not problems:
        print("cli: PASS (piped output plain, --no-color honored)")
    return problems

# ---------------------------------------------------------------------------
# check: xaml
# ---------------------------------------------------------------------------

XAML_FILES = ("winui3\\App.xaml", "winui3\\MainWindow.xaml")
INLINE_PROPS = ("Foreground=", "FontSize=", "FontFamily=")

def check_xaml():
    """No hand-placed inline Foreground/FontSize/FontFamily literals."""
    problems = []
    for rel in XAML_FILES:
        path = os.path.join(REPO, rel)
        if not os.path.isfile(path):
            problems.append("%s: file missing" % rel)
            continue
        with open(path, "r", encoding="utf-8") as fh:
            lines = fh.readlines()
        for i, line in enumerate(lines, 1):
            for prop in INLINE_PROPS:
                idx = line.find(prop)
                if idx < 0:
                    continue
                value = line[idx + len(prop):].lstrip()
                if value.startswith('"'):
                    value = value[1:]
                if value.startswith("{"):
                    continue  # ThemeResource / resource reference is fine
                problems.append("%s:%d: inline %s literal" % (rel, i, prop.strip("=")))
    if not problems:
        print("xaml: PASS (no inline style literals in %s)" % ", ".join(XAML_FILES))
    return problems

# ---------------------------------------------------------------------------
# check: setup
# ---------------------------------------------------------------------------

def check_setup():
    """setup.ps1 Stage/Die use the shared tag vocabulary."""
    path = os.path.join(REPO, "setup.ps1")
    if not os.path.isfile(path):
        return ["setup.ps1 missing"]
    with open(path, "r", encoding="utf-8") as fh:
        src = fh.read()

    problems = []
    if "[INFO]  Step" not in src:
        problems.append("setup.ps1: Stage does not emit the [INFO]  Step tag")
    if "[ERROR]" not in src or "[WARN]  Fix" not in src:
        problems.append("setup.ps1: Die does not emit [ERROR]/[WARN]  tags")
    if "[ OK ]" not in src:
        problems.append("setup.ps1: final completion line lacks the [ OK ] tag")
    if not problems:
        print("setup: PASS (Stage/Die use INFO/OK/WARN/ERROR tags)")
    return problems

# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("cli", "xaml", "setup"):
        p = sub.add_parser(name)
        if name == "cli":
            p.add_argument("--exe", required=True)
    check = sub.add_parser("check")
    check.add_argument("--all", action="store_true", help="run every check")
    check.add_argument("--exe", default=None)
    args = ap.parse_args()

    if args.cmd == "check":
        if not args.all:
            ap.error("check requires --all")
        checks = []
        if args.exe:
            checks += check_cli(args.exe)
        else:
            print("cli: SKIPPED (pass --exe to also check piped CLI output)")
        checks += check_xaml()
        checks += check_setup()
    elif args.cmd == "cli":
        checks = check_cli(args.exe)
    elif args.cmd == "xaml":
        checks = check_xaml()
    elif args.cmd == "setup":
        checks = check_setup()

    for problem in checks:
        print("problem: %s" % problem, file=sys.stderr)
    sys.exit(1 if checks else 0)


if __name__ == "__main__":
    main()