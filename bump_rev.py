#!/usr/bin/env python3
"""
bump_rev.py  —  bumps Rev in version.h on every build.
Run from project root.
"""

import re
from pathlib import Path

VERSION_FILE = Path("src/version.h")

text = VERSION_FILE.read_text(encoding="utf-8")

def get(name):
    m = re.search(rf"const(?:expr)? int {name}\s*=\s*(-?\d+)", text)
    return int(m.group(1)) if m else 0

maj, min_, bld, rev = get("Maj"), get("Min"), get("Bld"), get("Rev")

rev += 1
new_text, n = re.subn(r"(const(?:expr)? int Rev\s*=\s*)-?\d+", rf"\g<1>{rev}", text)
if n == 0:
    raise SystemExit("[bump_rev] ERROR: no 'Rev' declaration found — check version.h syntax")
VERSION_FILE.write_text(new_text, encoding="utf-8")
print(f"[bump_rev] {maj}.{min_}.{bld}.{rev}")