#!/usr/bin/env python3
"""
check_comments.py

Checks "boxed" tiered comment blocks (Tier 1 '#', Tier 2 '*', Tier 3 '~') in
.h/.cpp files under <root>/src against COMMENT_STYLE.md, following the same
box-detection approach as rewrap_comments.py.

Two independent passes:

  MECHANICAL (auto-fixable) -- applied to the file, reviewed like
  rewrap_comments.py (one diff per file, Enter to apply / Esc to skip),
  or applied unconditionally with --auto:
    - a tier marker char reused as an internal/closing separator instead
      of a plain hyphen line ("marker only on the first line" rule)
    - a hyphen separator whose width doesn't match the block's opening
      marker line
    - em/en dashes and curly quotes/ellipsis inside comment text,
      normalized to plain ASCII

  ADVISORY (logged only -- these need judgment, not a rewrite):
    - banned phrases (process narration / hedge-filler) from the style guide
    - Doxygen/Javadoc tags
    - unresolvable non-ASCII characters
    - DESCRIPTION over its tier's line cap
    - a LISTING entry spanning more than 2 physical lines
    - two same-marker blocks sitting back-to-back on one construct
      (cap-dodging)
    - a bare (contentless) block with no "(see: ...)" cross-reference

The advisory log is always printed, with or without --auto, and always saved
to <root>/logs/check_comments.log (created if needed) -- that's the file
meant to go to an AI pass. --log FILE writes it somewhere else instead.

Usage:
    python3 tools/check_comments.py [--root ROOT] [--auto] [--log FILE]
    python3 tools/check_comments.py [--auto] [--log FILE] FILE [FILE ...]

With no positional arguments, walks <root>/src and checks every .h/.cpp
file found there. If one or more FILE arguments are given, only those
specific files are checked instead (any extension accepted, --root ignored).

Review mode (default, no --auto):
    If a file has any mechanical fixes, its full diff is shown and the
    script waits for a single keypress: Enter applies every mechanical fix
    in that file, Esc skips all of them. The advisory log for that file is
    printed either way.

Automatic mode (--auto):
    Every mechanical fix is applied and every changed file is written,
    with no prompts.

By default ROOT is the parent directory of wherever this script lives
(i.e. it assumes the script is at <root>/tools/check_comments.py).

Accepted false positives (advisory only):
    Some bare-block-no-crossref findings get reviewed once and accepted as
    correct as-is -- e.g. a block whose NAME is genuinely self-explanatory,
    with nothing else to point a "(see: ...)" at. Rather than mark that in
    the source (which just raises "why does this comment say that?" for a
    reader who doesn't know this script exists), accepted NAMEs go in an
    ignore file: <root>/tools/check_comments_ignore.txt by default, or
    wherever --ignore-file points.

    Format: one exact block NAME per line (whatever text follows the tier
    marker's separator on the NAME row -- "SETTING", "Fnv1a64", etc).
    Blank lines and lines starting with '#' are ignored -- put the
    reasoning for each entry (or group of entries) in a comment above it.
    Matching is global (not scoped to a file or line), and only ever
    applies to the bare-block-no-crossref check -- so it stays valid
    across edits that move the block around, at the cost of also
    suppressing that same check anywhere else the identical NAME text
    shows up as a bare block. Fine for a distinctive name; worth a second
    thought for a common short one.
"""

import argparse
import difflib
import os
import re
import sys

# ---------------------------------------------------------------------------
# Regexes / constants
# ---------------------------------------------------------------------------

DASH_LINE_RE = re.compile(r'^(?P<indent>[ \t]*)//(?P<dashes>-{10,})[ \t]*$')
MARKER_LINE_RE = re.compile(r'^(?P<indent>[ \t]*)//(?P<mchar>[#*~])(?P=mchar){9,}[ \t]*$')
COMMENT_LINE_RE = re.compile(r'^(?P<indent>[ \t]*)//(?P<rest>.*)$')

# A '//' line whose content is *only* whitespace and separator/marker
# characters -- catches a run that would otherwise match DASH_LINE_RE or
# MARKER_LINE_RE if not for a stray space or tab splitting it up. Lines
# like this fail both of the strict patterns above, which means they're
# structurally invisible to find_blocks() -- not even flagged as a
# malformed block, since as far as the parser's concerned no block was
# ever opened there.
NEAR_MISS_SEPARATOR_RE = re.compile(r'^[ \t]*//(?P<body>[ \t#*~-]+)$')

TIER_INFO = {
    '#': {'label': 'Tier 1', 'listing': 'CONTENT', 'desc_cap': 20},
    '*': {'label': 'Tier 2', 'listing': 'STRUCTURE', 'desc_cap': 10},
    '~': {'label': 'Tier 3', 'listing': None, 'desc_cap': 10},
}

# Banned phrases from COMMENT_STYLE.md's "Banned content" section.
# The guide's own examples are "e.g." (non-exhaustive) -- this list is
# deliberately (sorry) broader than the short list shown to humans.
PROCESS_NARRATION_PHRASES = [
    "deliberately", "for now", "not decided yet", "user decided",
    "see TODO", "changed this because",
]
HEDGE_FILLER_PHRASES = [
    "basically", "essentially", "note that", "it's worth noting",
    "in order to", "rather than",
]

def _phrase_re(phrase):
    return re.compile(r'\b' + re.escape(phrase) + r'\b', re.IGNORECASE)

BANNED_PHRASE_PATTERNS = (
    [(p, 'process-narration', _phrase_re(p)) for p in PROCESS_NARRATION_PHRASES] +
    [(p, 'hedge-filler', _phrase_re(p)) for p in HEDGE_FILLER_PHRASES]
)

DOXYGEN_TAG_RE = re.compile(
    r'[@\\](param|brief|return|returns|throws|throw|note|see|deprecated|'
    r'author|file|class|struct|enum|def|ingroup|code|endcode|warning)\b',
    re.IGNORECASE,
)

# Non-ASCII characters that are safe to normalize automatically.
ASCII_FIX_MAP = {
    '\u2014': '-',   # em dash
    '\u2013': '-',   # en dash
    '\u2018': "'",   # left single quote
    '\u2019': "'",   # right single quote
    '\u201c': '"',   # left double quote
    '\u201d': '"',   # right double quote
    '\u2026': '...', # ellipsis
}


# ---------------------------------------------------------------------------
# Issue records
# ---------------------------------------------------------------------------

class Issue:
    """One advisory finding -- logged, never auto-applied."""
    def __init__(self, line_no, check_id, message, context='', name_text=None):
        self.line_no = line_no      # 1-based
        self.check_id = check_id
        self.message = message
        self.context = context
        self.name_text = name_text  # set for checks an ignore-by-name file can match against

    def format(self):
        out = f'  line {self.line_no:<5} [{self.check_id}] {self.message}'
        if self.context:
            out += f'\n              context: "{self.context}"'
        return out


# ---------------------------------------------------------------------------
# Pass A: global line-by-line checks (banned phrases, doxygen, non-ascii)
# ---------------------------------------------------------------------------

def normalize(raw):
    return raw[1:] if raw.startswith(' ') else raw


def is_structured(norm):
    """Same heuristic as rewrap_comments.py: a line is 'structured'
    (part of an aligned listing / code sample) rather than prose if it's
    indented past the normal '// ' prefix, or contains column-alignment
    (2+ consecutive spaces)."""
    if norm.strip() == '':
        return False
    if norm.startswith(' ') or norm.startswith('\t'):
        return True
    if '  ' in norm:
        return True
    return False


def scan_global(lines):
    """Banned phrases, Doxygen tags, unresolved non-ASCII -- across every
    '//' comment line in the file, independent of block structure, plus
    the fixed text for the ASCII-normalizable characters."""
    issues = []
    fixed_lines = list(lines)

    for idx, raw in enumerate(lines):
        m = COMMENT_LINE_RE.match(raw)
        if not m:
            continue
        content = m.group('rest')
        line_no = idx + 1

        for phrase, category, pat in BANNED_PHRASE_PATTERNS:
            if pat.search(content):
                issues.append(Issue(
                    line_no, f'banned-phrase:{category}',
                    f'"{phrase}" -- rephrase as a plain fact, or cut it',
                    context=raw.strip(),
                ))

        for dm in DOXYGEN_TAG_RE.finditer(content):
            issues.append(Issue(
                line_no, 'doxygen-tag',
                f'"{dm.group(0)}" -- this style uses no doc-gen tags',
                context=raw.strip(),
            ))

        if not (DASH_LINE_RE.match(raw) or MARKER_LINE_RE.match(raw)):
            nm = NEAR_MISS_SEPARATOR_RE.match(raw)
            if nm:
                body = nm.group('body').replace(' ', '').replace('\t', '')
                if len(body) >= 10 and len(set(body)) == 1:
                    issues.append(Issue(
                        line_no, 'malformed-separator',
                        f"looks like a {body[0]!r} separator/marker line, but "
                        f"whitespace is breaking up the run -- invisible to "
                        f"block parsing as-is, not just misformatted",
                        context=raw.strip(),
                    ))

        fixed = raw
        replaced = []
        for bad, good in ASCII_FIX_MAP.items():
            if bad in fixed:
                replaced.append((bad, good))
            fixed = fixed.replace(bad, good)
        if fixed != raw:
            fixed_lines[idx] = fixed
            swaps = ', '.join(f'{bad!r} -> {good!r}' for bad, good in replaced)
            issues.append(Issue(
                line_no, 'ascii-normalized',
                f'{swaps} -- normalized to ASCII',
                context=raw.strip(),
            ))

        remainder = fixed
        for ch in remainder:
            if ord(ch) > 127:
                issues.append(Issue(
                    line_no, 'non-ascii-unresolved',
                    f'non-ASCII character {ch!r} has no safe auto-fix -- reword by hand',
                    context=raw.strip(),
                ))
                break

    return issues, fixed_lines


# ---------------------------------------------------------------------------
# Pass B: block structural checks
# ---------------------------------------------------------------------------

def find_blocks(lines):
    """Find every well-formed Tier 1/2/3 block: an opening marker line,
    a NAME line, then one or more separator-bounded segments, ending on a
    closing separator. A block is 'well-formed' the same way
    rewrap_comments.py defines it for its boxes: at least one separator
    pair, and nothing dangling after the last one.

    Returns (blocks, malformed): blocks is a list of dicts, each recording
    any separator line that wrongly reused the marker char instead of a
    plain hyphen (that's the mechanical fix), and each segment's raw
    content for the advisory cap checks. malformed is a list of
    (name_line_idx, name_text) for marker+NAME pairs that never reached a
    proper close, logged as advisory rather than silently dropped.
    """
    blocks = []
    malformed = []  # (name_line_idx, name_text) -- looked like a block, never closed
    i = 0
    n = len(lines)

    while i < n:
        mm = MARKER_LINE_RE.match(lines[i])
        if not mm:
            i += 1
            continue

        marker_char = mm.group('mchar')
        marker_len = len(lines[i]) - len(mm.group('indent')) - 2  # minus '//'
        indent = mm.group('indent')
        block_start = i
        i += 1

        if i >= n:
            break
        name_comm = COMMENT_LINE_RE.match(lines[i])
        if not name_comm or DASH_LINE_RE.match(lines[i]) or MARKER_LINE_RE.match(lines[i]):
            continue  # not actually a block, just a marker-shaped line; skip
        name_line_idx = i
        name_text = name_comm.group('rest').strip()
        i += 1

        # Segments are collected the same way rewrap_comments.py collects
        # them: the *first* separator only marks where content starts (it
        # closes nothing, since nothing precedes it) -- every separator
        # after that closes the segment accumulated since the previous one.
        #
        # A same-tier marker line hit while a segment is still open (i.e.
        # before any closing dash) is treated as that block's own
        # (misused) closing separator and ends the block right there --
        # it can never be ambiguous with a *later*, independent block,
        # because an independent block's marker only ever appears once
        # the previous block has already closed cleanly (current == []).
        # Any other marker line encountered mid-block is left unconsumed
        # so the outer loop picks it up fresh.
        separators = []   # (kind, line_idx, width) kind is 'dash' or 'marker'(wrong)
        segments = []
        current = []
        first_sep = True

        while i < n:
            dm = DASH_LINE_RE.match(lines[i])
            mk = MARKER_LINE_RE.match(lines[i])

            if dm:
                if not first_sep:
                    segments.append(current)
                    current = []
                separators.append(('dash', i, len(dm.group('dashes'))))
                first_sep = False
                i += 1
                continue

            if mk:
                if current == [] or mk.group('mchar') != marker_char:
                    # Either this block already closed cleanly and what
                    # follows belongs to something else, or a *different*
                    # tier's marker showed up mid-segment (genuinely
                    # malformed) -- either way, don't consume this line.
                    break
                # Same-tier marker misused as our closing separator.
                segments.append(current)
                current = []
                separators.append(('marker', i, len(lines[i]) - len(indent) - 2))
                i += 1
                break

            comm = COMMENT_LINE_RE.match(lines[i])
            if not comm:
                break
            current.append((i, comm.group('rest')))
            i += 1

        well_formed = len(separators) >= 1 and current == []

        if not well_formed:
            if separators:
                malformed.append((name_line_idx, name_text))
            i = name_line_idx + 1  # rewind; wasn't a real block after all
            continue

        blocks.append({
            'marker_char': marker_char,
            'marker_len': marker_len,
            'indent': indent,
            'block_start': block_start,
            'name_line_idx': name_line_idx,
            'name_text': name_text,
            'separators': separators,
            'segments': segments,
            'end': i,
        })

    return blocks, malformed


def classify_segments(block):
    """Return (listing_seg_or_None, description_seg_or_None) for a block,
    using segment order when unambiguous (2 segments = LISTING then
    DESCRIPTION) and the structured/prose heuristic when there's only one
    segment and the tier has a LISTING section at all."""
    tier = TIER_INFO[block['marker_char']]
    segs = block['segments']

    if len(segs) == 0:
        return None, None
    if len(segs) >= 2:
        return segs[0], segs[1]

    seg = segs[0]
    if not seg:
        return None, None  # bare cross-ref block
    if tier['listing'] is None:
        return None, seg  # Tier 3: never a listing

    structured = sum(1 for _, raw in seg if is_structured(normalize(raw)))
    prose = len(seg) - structured
    if structured >= prose:
        return seg, None
    return None, seg


def check_blocks(lines, blocks, malformed):
    """Advisory issues (cap overflow, entry length, cap-dodging, bare
    blocks, malformed blocks) plus the list of mechanical separator fixes
    to apply."""
    issues = []
    mech_fixes = []  # (line_idx, new_text)

    for name_line_idx, name_text in malformed:
        issues.append(Issue(
            name_line_idx + 1, 'malformed-block',
            f"{name_text!r}: opened but never reached a proper closing "
            f"separator -- not checked further",
        ))

    for b in blocks:
        tier = TIER_INFO[b['marker_char']]

        # -- mechanical: marker reused as separator --------------------
        for kind, idx, width in b['separators']:
            if kind == 'marker':
                new_line = f"{b['indent']}//{'-' * b['marker_len']}"
                mech_fixes.append((idx, new_line))
                issues.append(Issue(
                    idx + 1, 'marker-as-separator',
                    f"{tier['label']} marker '{b['marker_char']}' reused on an "
                    f"internal/closing separator -- fixed to a hyphen line",
                ))

        # -- mechanical: separator width mismatch -----------------------
        for kind, idx, width in b['separators']:
            if width != b['marker_len'] and kind == 'dash':
                new_line = f"{b['indent']}//{'-' * b['marker_len']}"
                mech_fixes.append((idx, new_line))
                issues.append(Issue(
                    idx + 1, 'separator-width',
                    f"separator is {width} chars, block opens at {b['marker_len']} -- width fixed",
                ))

        # -- advisory: tier 3 shouldn't have a listing segment ----------
        if tier['listing'] is None and len(b['segments']) >= 2:
            issues.append(Issue(
                b['name_line_idx'] + 1, 'tier3-has-listing',
                f"{b['name_text']!r}: Tier 3 has no LISTING section, but this "
                f"block has {len(b['segments'])} segments -- check the split",
            ))

        listing_seg, desc_seg = classify_segments(b)

        # -- advisory: DESCRIPTION cap ------------------------------------
        if desc_seg is not None and len(desc_seg) > tier['desc_cap']:
            issues.append(Issue(
                b['name_line_idx'] + 1, 'description-cap',
                f"{b['name_text']!r}: DESCRIPTION is {len(desc_seg)} lines, "
                f"{tier['label']} cap is {tier['desc_cap']} -- compress, "
                f"move detail down a tier, or split across .h/.cpp",
            ))

        # -- advisory: LISTING entry > 2 physical lines --------------------
        if listing_seg is not None:
            entry_start_idx = None
            entry_len = 0

            def flush(entry_start_idx, entry_len):
                if entry_start_idx is not None and entry_len > 2:
                    issues.append(Issue(
                        entry_start_idx + 1, 'listing-entry-cap',
                        f"{b['name_text']!r}: {tier['listing']} entry spans "
                        f"{entry_len} lines, cap is 2",
                    ))

            for line_idx, raw in listing_seg:
                norm = normalize(raw)
                continuation = norm.startswith(' ') or norm.startswith('\t')
                if is_structured(norm) and not continuation:
                    flush(entry_start_idx, entry_len)
                    entry_start_idx, entry_len = line_idx, 1
                elif continuation and entry_start_idx is not None:
                    entry_len += 1
                else:
                    flush(entry_start_idx, entry_len)
                    entry_start_idx, entry_len = None, 0
            flush(entry_start_idx, entry_len)

        # -- advisory: bare block with no cross-reference -------------------
        if len(b['segments']) <= 1 and not (listing_seg or desc_seg):
            if '(see:' not in b['name_text']:
                issues.append(Issue(
                    b['name_line_idx'] + 1, 'bare-block-no-crossref',
                    f"{b['name_text']!r}: empty block body with no "
                    f"'(see: file)' reference -- intentional, or missing content?",
                    name_text=b['name_text'],
                ))

    # -- advisory: cap-dodging (adjacent same-tier blocks, same construct) --
    # Adjacency alone is normal -- most functions/structs sit right next to
    # each other with no blank line. Only the same identifier repeated is a
    # signal this might be one construct split to double its cap. (This can
    # still false-positive on overloads that share a bare function name --
    # the guide's NAME format doesn't include parameters -- so it's worth a
    # glance, not an automatic rewrite.)
    for prev, nxt in zip(blocks, blocks[1:]):
        if (nxt['block_start'] == prev['end']
                and nxt['marker_char'] == prev['marker_char']
                and nxt['name_text'] == prev['name_text']):
            issues.append(Issue(
                nxt['block_start'] + 1, 'cap-dodge',
                f"{TIER_INFO[nxt['marker_char']]['label']} block back-to-back with "
                f"the one ending at line {prev['end']}, same name {prev['name_text']!r} "
                f"-- counts as one block against the cap (unless these are overloads)",
            ))

    return issues, mech_fixes


# ---------------------------------------------------------------------------
# Review UI (same interaction pattern as rewrap_comments.py)
# ---------------------------------------------------------------------------

def get_key():
    if not sys.stdin.isatty():
        line = sys.stdin.readline()
        return 'esc' if line == '' else 'enter'
    try:
        import termios
        import tty
        fd = sys.stdin.fileno()
        old_settings = termios.tcgetattr(fd)
        try:
            tty.setraw(fd)
            ch = sys.stdin.read(1)
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
    except (ImportError, AttributeError):
        try:
            import msvcrt
            ch = msvcrt.getch().decode(errors='ignore')
        except ImportError:
            line = sys.stdin.readline()
            return 'enter' if line else 'esc'
    if ch in ('\r', '\n'):
        return 'enter'
    if ch == '\x1b':
        return 'esc'
    return None


def review_file(path, original, fixed):
    print(f'\n----- {path} -----')
    diff = difflib.unified_diff(original, fixed, lineterm='')
    for line in diff:
        print(line)
    print('[Enter] apply mechanical fixes   [Esc] skip ', end='', flush=True)
    while True:
        key = get_key()
        print()
        if key == 'enter':
            return True
        if key == 'esc':
            return False
        print('[Enter] apply mechanical fixes   [Esc] skip ', end='', flush=True)


# ---------------------------------------------------------------------------
# Ignore file (accepted bare-block-no-crossref names)
# ---------------------------------------------------------------------------

def load_ignore_names(path):
    """Parse a plain list of block NAMEs (one per line, '#' comments and
    blank lines skipped) into a set. Matched only against the
    bare-block-no-crossref check, by exact NAME text -- global, not
    scoped to a file or line, so it stays valid across edits that move
    the block around."""
    names = set()
    if not path or not os.path.isfile(path):
        return names

    with open(path, 'r', encoding='utf-8') as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line or line.startswith('#'):
                continue
            names.add(line)
    return names


# ---------------------------------------------------------------------------
# Per-file processing
# ---------------------------------------------------------------------------

def process_file(path, auto=False):
    with open(path, 'r', encoding='utf-8') as f:
        original_text = f.read()
    lines = original_text.splitlines()

    global_issues, ascii_fixed_lines = scan_global(lines)
    blocks, malformed = find_blocks(ascii_fixed_lines)
    block_issues, mech_fixes = check_blocks(ascii_fixed_lines, blocks, malformed)

    fixed_lines = list(ascii_fixed_lines)
    for idx, new_text in mech_fixes:
        fixed_lines[idx] = new_text

    all_issues = sorted(global_issues + block_issues, key=lambda iss: iss.line_no)
    changed = fixed_lines != lines

    if changed:
        if auto:
            apply_change = True
        else:
            apply_change = review_file(path, lines, fixed_lines)
        if apply_change:
            newline = '\r\n' if '\r\n' in original_text else '\n'
            new_content = newline.join(fixed_lines)
            if original_text.endswith('\n') and not new_content.endswith('\n'):
                new_content += newline
            with open(path, 'w', encoding='utf-8', newline='') as f:
                f.write(new_content)
        else:
            changed = False

    return changed, all_issues


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    default_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser.add_argument('--root', default=default_root,
                         help='project root (default: parent directory of this script, i.e. <root>/tools/..)')
    parser.add_argument('--auto', action='store_true',
                         help='apply all mechanical fixes automatically and write every '
                              'changed file, without prompting (default is to review each '
                              "file's mechanical fixes with a diff)")
    parser.add_argument('--log', metavar='FILE',
                         help='write the advisory log to FILE, in addition to stdout '
                              '(default: <root>/logs/check_comments.log -- shared by any '
                              'tool that adopts the same convention)')
    parser.add_argument('--ignore-file', metavar='FILE',
                         help='block NAMEs accepted as fine to stay bare, one per line '
                              '(default: <root>/tools/check_comments_ignore.txt, if it '
                              'exists) -- see the "Accepted false positives" section above')
    parser.add_argument('files', nargs='*', metavar='FILE',
                         help='one or more specific files to check, instead of walking '
                              '<root>/src. When given, --root is ignored for file '
                              'selection (though the default log path still uses it), '
                              'and any file extension is accepted.')
    args = parser.parse_args()

    tool_stem = os.path.splitext(os.path.basename(__file__))[0]
    log_path = args.log or os.path.join(args.root, 'logs', f'{tool_stem}.log')
    ignore_file = args.ignore_file or os.path.join(args.root, 'tools', 'check_comments_ignore.txt')
    ignore_names = load_ignore_names(ignore_file)

    if args.files:
        paths = args.files
        for p in paths:
            if not os.path.isfile(p):
                print(f'error: {p} does not exist', file=sys.stderr)
                sys.exit(1)
    else:
        src_dir = os.path.join(args.root, 'src')
        if not os.path.isdir(src_dir):
            print(f'error: {src_dir} does not exist', file=sys.stderr)
            sys.exit(1)
        paths = []
        for dirpath, _dirnames, filenames in os.walk(src_dir):
            for fn in filenames:
                if fn.endswith('.h') or fn.endswith('.cpp'):
                    paths.append(os.path.join(dirpath, fn))

    log_lines = []
    total_fixed_files = 0
    total_issue_files = 0
    total_issues = 0
    used_ignore_names = set()

    for path in paths:
        changed, issues = process_file(path, auto=args.auto)
        kept = []
        for iss in issues:
            if iss.check_id == 'bare-block-no-crossref' and iss.name_text in ignore_names:
                used_ignore_names.add(iss.name_text)
            else:
                kept.append(iss)
        issues = kept
        if changed:
            total_fixed_files += 1
        if issues:
            total_issue_files += 1
            total_issues += len(issues)
            header = f'{path}  ({len(issues)} issue(s))'
            log_lines.append(header)
            print('\n' + header)
            for iss in issues:
                text = iss.format()
                log_lines.append(text)
                print(text)

    summary = (f'\n{total_issues} advisory issue(s) in {total_issue_files} file(s). '
               f'{total_fixed_files} file(s) had mechanical fixes applied.')
    print(summary)
    log_lines.append(summary.strip())

    stale = sorted(ignore_names - used_ignore_names)
    if stale:
        stale_header = (f'\n{len(stale)} stale ignore-file entry(ies) in {ignore_file} '
                         f'-- NAME no longer matches any bare-block-no-crossref finding '
                         f'(renamed, documented, or removed?):')
        print(stale_header)
        log_lines.append(stale_header.strip())
        for name in stale:
            stale_line = f'  {name}'
            print(stale_line)
            log_lines.append(stale_line)

    os.makedirs(os.path.dirname(os.path.abspath(log_path)), exist_ok=True)
    with open(log_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(log_lines) + '\n')
    print(f'Log written to {log_path}')


if __name__ == '__main__':
    main()