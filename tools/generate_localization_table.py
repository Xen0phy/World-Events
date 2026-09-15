#!/usr/bin/env python3
"""
generate_localization_table.py  -  regenerates a kLocalizationTable header
(default: src/generated/localization_table_ui.generated.h) from a
localization CSV (default: resources/localization/ui_strings.csv; see
LocalizationEntry in src/core/localization_table.h). Run from the project
root; wired into CMakeLists.txt as a custom command per CSV/header pair so
it reruns automatically whenever that CSV (or this script) changes - no
manual step needed for a normal build.

--csv/--header/--generated override the default ui_strings.csv pair - e.g.
for resources/localization/event_names.csv, once every language column is
filled in and a matching CMakeLists.txt custom command is added.

--symbol-prefix names the emitted table/count symbols (default: kLocalization,
giving kLocalizationTable/kLocalizationCount). A second generated header
included in the same translation unit as the default one needs its own
prefix, or the two collide on both symbol names.

Language columns are read from WE_LANGUAGE_LIST in localization_table.h, not
hardcoded here, so the CSV's required columns stay in sync with the addon's
actual language list on their own.

A row with identifier "#" is a section banner: its first language column
(the fallback language, first in WE_LANGUAGE_LIST) holds the banner text,
every other column must be blank. A row with everything blank is a plain
spacer line. Any other row is a translatable string and every language
column on it must be non-empty - this is what actually keeps languages in
sync now that hand-editing no longer forces it structurally: an empty cell
fails the build instead of shipping a silent gap.
"""

import argparse
import csv
import re
import sys
from pathlib import Path

DEFAULT_HEADER    = Path("src/core/localization_table.h")
DEFAULT_CSV       = Path("resources/localization/ui_strings.csv")
DEFAULT_GENERATED = Path("src/generated/localization_table_ui.generated.h")

WE_LANG_RE = re.compile(r'WE_LANG\(\s*\w+\s*,\s*"([^"]+)"\s*\)')


def get_language_codes(source_header: Path) -> list[str]:
    """Pulls language codes out of WE_LANGUAGE_LIST, in declared order."""
    text = source_header.read_text(encoding="utf-8")
    m = re.search(r'#define WE_LANGUAGE_LIST(.*?)\n\n', text, re.S)
    if not m:
        raise SystemExit(
            f"[generate_localization_table] ERROR: couldn't find "
            f"WE_LANGUAGE_LIST in {source_header}"
        )
    codes = WE_LANG_RE.findall(m.group(1))
    if not codes:
        raise SystemExit(
            f"[generate_localization_table] ERROR: WE_LANGUAGE_LIST in "
            f"{source_header} matched but no WE_LANG(...) entries parsed"
        )
    return codes


def cpp_escape(text: str) -> str:
    out = []
    for ch in text:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        else:
            out.append(ch)
    return "".join(out)


def emit_field_lines(text: str, indent: str) -> list[str]:
    """One field's value as one or more adjacent C++ string literals, split
    at each embedded newline so a multi-line tooltip reads as one literal
    per visual line instead of one very long line."""
    pieces = text.split("\n")
    lines = []
    for i, piece in enumerate(pieces):
        suffix = "\\n" if i < len(pieces) - 1 else ""
        lines.append(f'{indent}"{cpp_escape(piece)}{suffix}"')
    return lines


FMT_PLACEHOLDER_RE = re.compile(r'%[-+ #0-9.]*(?:ll|l|h)?[a-zA-Z%]')


def check_fmt_placeholders(identifier: str, values: dict[str, str], warnings: list[str]) -> None:
    if not identifier.endswith("_FMT"):
        return
    codes = list(values.keys())
    reference = FMT_PLACEHOLDER_RE.findall(values[codes[0]])
    for code in codes[1:]:
        found = FMT_PLACEHOLDER_RE.findall(values[code])
        if found != reference:
            warnings.append(
                f"{identifier}: format placeholders differ between "
                f"'{codes[0]}' {reference} and '{code}' {found}"
            )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--header", type=Path, default=DEFAULT_HEADER,
        help=f"header WE_LANGUAGE_LIST is read from (default: {DEFAULT_HEADER})")
    parser.add_argument("--csv", type=Path, default=DEFAULT_CSV,
        help=f"source CSV (default: {DEFAULT_CSV})")
    parser.add_argument("--generated", type=Path, default=DEFAULT_GENERATED,
        help=f"generated header to write (default: {DEFAULT_GENERATED})")
    parser.add_argument("--symbol-prefix", default="kLocalization",
        help="prefix for the emitted table/count symbols (default: kLocalization)")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    source_header  = args.header
    source_csv     = args.csv
    generated_file = args.generated
    table_name     = f"{args.symbol_prefix}Table"
    count_name     = f"{args.symbol_prefix}Count"

    codes = get_language_codes(source_header)
    expected_header = ["identifier"] + codes + ["note"]

    if not source_csv.exists():
        raise SystemExit(f"[generate_localization_table] ERROR: {source_csv} not found")

    with source_csv.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        if reader.fieldnames != expected_header:
            raise SystemExit(
                f"[generate_localization_table] ERROR: {source_csv} header is "
                f"{reader.fieldnames}, expected {expected_header} (from "
                f"WE_LANGUAGE_LIST in {source_header})"
            )
        rows = list(reader)

    seen_identifiers: set[str] = set()
    warnings: list[str] = []
    lines = [
        "//################################################################################",
        f"// {generated_file.name}   (see: src/core/localization_table.h)",
        "//--------------------------------------------------------------------------------",
        f"// Auto-generated by tools/generate_localization_table.py from",
        f"// {source_csv} - do not hand-edit, and do not commit (gitignored).",
        "// Regenerated automatically as part of the normal CMake build whenever the",
        "// CSV or the generator script changes.",
        "//--------------------------------------------------------------------------------",
        "",
        f"static constexpr LocalizationEntry {table_name}[] = {{",
        "",
    ]

    for row_no, row in enumerate(rows, start=2):  # +1 header, +1 1-indexed
        identifier = row["identifier"].strip()
        values = {code: row[code] for code in codes}
        note = row["note"].strip()

        if identifier == "" and not any(v.strip() for v in values.values()) and not note:
            lines.append("")
            continue

        if identifier == "#":
            banner_text = values[codes[0]].strip()
            other_filled = [c for c in codes[1:] if values[c].strip()]
            if not banner_text:
                raise SystemExit(
                    f"[generate_localization_table] ERROR: {source_csv}:{row_no} "
                    f"banner row has no text in '{codes[0]}'"
                )
            if other_filled or note:
                raise SystemExit(
                    f"[generate_localization_table] ERROR: {source_csv}:{row_no} "
                    f"banner row must only use '{codes[0]}' - also has content in "
                    f"{other_filled + (['note'] if note else [])}"
                )
            lines.append(f"    //_ {banner_text}")
            lines.append("")
            continue

        if not identifier.startswith("WE_"):
            raise SystemExit(
                f"[generate_localization_table] ERROR: {source_csv}:{row_no} "
                f"identifier '{identifier}' doesn't start with 'WE_'"
            )
        if identifier in seen_identifiers:
            raise SystemExit(
                f"[generate_localization_table] ERROR: {source_csv}:{row_no} "
                f"duplicate identifier '{identifier}'"
            )
        seen_identifiers.add(identifier)

        empty = [c for c in codes if not values[c].strip()]
        if empty:
            raise SystemExit(
                f"[generate_localization_table] ERROR: {source_csv}:{row_no} "
                f"'{identifier}' is missing text for: {empty}"
            )

        check_fmt_placeholders(identifier, values, warnings)

        lines.append(f'    {{ "{identifier}",')
        if note:
            lines.append(f"        //_ {note}")
        for i, code in enumerate(codes):
            field_lines = emit_field_lines(values[code], "        ")
            field_lines[-1] += " }," if i == len(codes) - 1 else ","
            lines.extend(field_lines)
        lines.append("")

    lines.append("};")
    lines.append("")
    lines.append(
        f"inline constexpr int {count_name} = "
        f"sizeof({table_name}) / sizeof({table_name}[0]);"
    )
    lines.append("")

    if not seen_identifiers:
        raise SystemExit(
            f"[generate_localization_table] ERROR: {source_csv} produced zero entries"
        )

    if warnings:
        for w in warnings:
            print(f"[generate_localization_table] ERROR: {w}")
        raise SystemExit(1)

    generated_file.parent.mkdir(parents=True, exist_ok=True)
    generated_file.write_text("\n".join(lines), encoding="utf-8")

    print(
        f"[generate_localization_table] wrote {generated_file} with "
        f"{len(seen_identifiers)} entries, {len(codes)} language(s)"
    )


if __name__ == "__main__":
    sys.exit(main())