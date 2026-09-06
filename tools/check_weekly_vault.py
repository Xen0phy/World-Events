#!/usr/bin/env python3
"""
check_weekly_vault.py - validates weekly_vault.cpp's g_CyclicWeeklyObjectives
table against the real CyclicGroup/Slot names in events_cyclic.cpp, run from
the project root as a build step that happens BEFORE bump_rev (see
CMakeLists.txt) - a typo here fails the build with the specific bad name,
instead of silently shipping a target that can never light up at runtime
(see weekly_vault.h's own comment on this).

Does NOT (and can't) validate titleKeywords against ArenaNet's live wording
- nothing offline can confirm that. This only checks the OTHER half of the
table: that every `targets` entry still resolves to a real (group, slot)
pair, and that no mapping is missing keywords/targets entirely. Core Boss
matching needs no check at all - see weekly_vault.h for why that half of
the old table was removed rather than validated.

Exits 0 and prints a one-line summary if everything resolves. Exits 1 and
prints every problem found (not just the first) if anything doesn't, so a
single run surfaces every typo at once instead of one-per-build-attempt.
"""

import re
import sys
from pathlib import Path

EVENTS_CYCLIC = Path("src/events/events_cyclic.cpp")
WEEKLY_VAULT = Path("src/integration/weekly_vault.cpp")


def strip_comments(text):
    # Line comments only - this codebase has no /* */ comments in either
    # table, and no string literal in either file ever contains "//".
    return "\n".join(re.sub(r"//.*$", "", line) for line in text.splitlines())


def split_top_level(s):
    """Split `s` on commas at brace-depth 0, respecting "..." string literals."""
    parts = []
    depth = 0
    in_str = False
    current = []
    for ch in s:
        if in_str:
            current.append(ch)
            if ch == '"':
                in_str = False
            continue
        if ch == '"':
            in_str = True
            current.append(ch)
        elif ch == "{":
            depth += 1
            current.append(ch)
        elif ch == "}":
            depth -= 1
            current.append(ch)
        elif ch == "," and depth == 0:
            parts.append("".join(current))
            current = []
        else:
            current.append(ch)
    if "".join(current).strip():
        parts.append("".join(current))
    return parts


def extract_braced(text, start_marker):
    """Content of the first balanced { ... } following start_marker's first '='."""
    idx = text.index(start_marker)
    eq = text.index("=", idx)
    brace_start = text.index("{", eq)
    depth = 0
    in_str = False
    for i in range(brace_start, len(text)):
        ch = text[i]
        if in_str:
            if ch == '"':
                in_str = False
            continue
        if ch == '"':
            in_str = True
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return text[brace_start + 1 : i]
    raise SystemExit(f"[check_weekly_vault] ERROR: unbalanced braces after '{start_marker}'")


def unwrap_braces(s, context):
    s = s.strip()
    if not (s.startswith("{") and s.endswith("}")):
        raise SystemExit(
            f"[check_weekly_vault] ERROR: expected a brace-enclosed entry in {context}, "
            f"got: {s[:60]!r}"
        )
    return s[1:-1]


def first_string(s):
    m = re.search(r'"([^"]*)"', s)
    return m.group(1) if m else None


def parse_cyclic_groups(text):
    """Returns {group_name: set(slot_names)} from events_cyclic.cpp."""
    body = extract_braced(text, "g_CyclicGroups")
    groups = {}
    for i, block in enumerate(split_top_level(body)):
        block = block.strip()
        if not block:
            continue
        inner = unwrap_braces(block, f"g_CyclicGroups entry #{i + 1}")
        parts = split_top_level(inner)
        if not parts:
            continue
        group_name = first_string(parts[0])
        if group_name is None:
            raise SystemExit(
                f"[check_weekly_vault] ERROR: g_CyclicGroups entry #{i + 1} doesn't "
                f"start with a quoted group name."
            )

        # The slots vector is whichever braced field's elements ALL start
        # with a quoted string - every other field on a CyclicGroup
        # (coordinates, period, color constant, std::nullopt, bool,
        # apiMapChestId) is either bare or not a list-of-quoted-things.
        slots = None
        for part in parts[1:]:
            part = part.strip()
            if not (part.startswith("{") and part.endswith("}")):
                continue
            slot_entries = [e for e in split_top_level(part[1:-1]) if e.strip()]
            if not slot_entries:
                continue
            names = [first_string(e) for e in slot_entries]
            if all(n is not None for n in names):
                slots = names
                break
        groups[group_name] = set(slots or [])
    return groups


def parse_weekly_mappings(text):
    """Returns [(mapping_index, [keyword,...], [(group,slot), ...]), ...]."""
    body = extract_braced(text, "g_CyclicWeeklyObjectives")
    mappings = []
    for i, block in enumerate(split_top_level(body)):
        block = block.strip()
        if not block:
            continue
        inner = unwrap_braces(block, f"g_CyclicWeeklyObjectives mapping #{i + 1}")
        parts = split_top_level(inner)
        if len(parts) != 2:
            raise SystemExit(
                f"[check_weekly_vault] ERROR: g_CyclicWeeklyObjectives mapping #{i + 1} "
                f"doesn't have exactly 2 top-level fields (titleKeywords, targets) - "
                f"found {len(parts)}. Check for a stray or missing comma."
            )
        keywords_part, targets_part = parts
        keywords = re.findall(r'"([^"]*)"', keywords_part)
        targets = re.findall(r'\{\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\}', targets_part)
        mappings.append((i + 1, keywords, targets))
    return mappings


def main():
    if not EVENTS_CYCLIC.exists() or not WEEKLY_VAULT.exists():
        raise SystemExit(
            f"[check_weekly_vault] ERROR: expected {EVENTS_CYCLIC} and {WEEKLY_VAULT} "
            f"- run this from the project root."
        )

    groups = parse_cyclic_groups(strip_comments(EVENTS_CYCLIC.read_text(encoding="utf-8")))
    mappings = parse_weekly_mappings(strip_comments(WEEKLY_VAULT.read_text(encoding="utf-8")))

    problems = []
    for mapping_idx, keywords, targets in mappings:
        if not keywords:
            problems.append(f"mapping #{mapping_idx}: titleKeywords is empty")
        if not targets:
            problems.append(f"mapping #{mapping_idx}: targets is empty")

        for group_name, slot_name in targets:
            if group_name not in groups:
                problems.append(
                    f"mapping #{mapping_idx} ({', '.join(keywords)!s}): "
                    f"group {group_name!r} not found in events_cyclic.cpp"
                )
                continue
            if slot_name not in groups[group_name]:
                problems.append(
                    f"mapping #{mapping_idx} ({', '.join(keywords)!s}): "
                    f"slot {slot_name!r} not found in group {group_name!r} "
                    f"(events_cyclic.cpp)"
                )

    if problems:
        print("[check_weekly_vault] FAILED - weekly_vault.cpp's "
              "g_CyclicWeeklyObjectives table has a problem:", file=sys.stderr)
        for p in problems:
            print(f"  - {p}", file=sys.stderr)
        raise SystemExit(1)

    total_targets = sum(len(t) for _, _, t in mappings)
    print(f"[check_weekly_vault] OK - {len(mappings)} mapping(s), "
          f"{total_targets} target(s), all resolve against events_cyclic.cpp")


if __name__ == "__main__":
    main()
