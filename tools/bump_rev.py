#!/usr/bin/env python3
"""
bump_rev.py  -  bumps Rev and refreshes the release timestamp (DateAndTime),
                both in src/addon/version.h, on every build.
                Rolls Bld up by 1 whenever Rev would exceed 9.
Run from project root.
"""

import re
from datetime import datetime
from pathlib import Path

VERSION_FILE = Path("src/addon/version.h")

text = VERSION_FILE.read_text(encoding="utf-8")


def get(name):
    m = re.search(rf"const(?:expr)? int {name}\s*=\s*(-?\d+)", text)
    return int(m.group(1)) if m else 0


maj, min_, bld, rev = get("Maj"), get("Min"), get("Bld"), get("Rev")

rev += 1
if rev > 9:
    rev = 0
    bld += 1

new_text, n_rev = re.subn(
    r"(const(?:expr)? int Rev\s*=\s*)-?\d+", rf"\g<1>{rev}", text
)
if n_rev == 0:
    raise SystemExit("[bump_rev] ERROR: no 'Rev' declaration found - check version.h syntax")

new_text, n_bld = re.subn(
    r"(const(?:expr)? int Bld\s*=\s*)-?\d+", rf"\g<1>{bld}", new_text
)
if n_bld == 0:
    raise SystemExit("[bump_rev] ERROR: no 'Bld' declaration found - check version.h syntax")

timestamp = datetime.now().strftime("%Y-%m-%d %H:%M")
new_text, n_date = re.subn(
    r'(inline const std::string DateAndTime\s*=\s*")[^"]*(";)',
    rf"\g<1>{timestamp}\g<2>",
    new_text,
)
if n_date == 0:
    raise SystemExit("[bump_rev] ERROR: no 'DateAndTime' declaration found - check version.h syntax")

VERSION_FILE.write_text(new_text, encoding="utf-8")

print(f"[bump_rev] {maj}.{min_}.{bld}.{rev}  ({timestamp})")