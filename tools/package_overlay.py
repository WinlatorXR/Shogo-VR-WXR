#!/usr/bin/env python3
"""Assembles the WinlatorXR container overlay for the Shogo VR port.

Copies the freshly built CShell.dll and the project's attribution files into a
tree shaped like the retail game folder:

    <out>/ShogoVR/CShell.dll
    <out>/ShogoVR/AUTHORS.txt
    <out>/ShogoVR/NOTICE.txt
    <out>/ShogoVR/ShogoVR.ini      (only if --ini is given)

Drop that ShogoVR folder into the game directory inside the container (next to
Client.exe and SHOGO.REZ) and launch Client.exe with the base archives first and
ShogoVR last, e.g.:

    Client.exe -windowtitle Shogo -rez "SHOGO.REZ" -rez "SOUND.REZ" \
               -rez "Custom" [ShogoP*.REZ ...] +multiplayer 0 -rez "ShogoVR"

Usage: package_overlay.py [--dll PATH] [--out DIR] [--ini PATH]
"""
import argparse, os, shutil, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", default=os.path.join(ROOT, "build", "out", "CShell.dll"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "overlay"))
    ap.add_argument("--ini", default=None)
    args = ap.parse_args()

    if not os.path.isfile(args.dll):
        sys.exit("missing %s - build the project first" % args.dll)

    mod = os.path.join(args.out, "ShogoVR")
    os.makedirs(mod, exist_ok=True)
    shutil.copy2(args.dll, os.path.join(mod, "CShell.dll"))
    shutil.copy2(os.path.join(ROOT, "AUTHORS.txt"), os.path.join(mod, "AUTHORS.txt"))
    shutil.copy2(os.path.join(ROOT, "NOTICE.md"), os.path.join(mod, "NOTICE.txt"))
    if args.ini:
        shutil.copy2(args.ini, os.path.join(mod, "ShogoVR.ini"))

    print("overlay written to %s" % mod)
    for name in sorted(os.listdir(mod)):
        print("  ", name)


if __name__ == "__main__":
    main()
