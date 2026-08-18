#!/usr/bin/env python3
"""
rewrap_comments.py

Finds "boxed" comment blocks in .h/.cpp files under <root>/src -- i.e. runs
of "//" comment lines bounded by one or more "//----...----" separator
lines -- and rewraps the *plain prose* inside them to fill the box's full
width, while leaving structured content untouched:

  - aligned reference tables / short-name-then-description lists
      // key                 description ...
      //                     continuation, indented to line up under the
      //                     description column
  - indented code samples / usage snippets
      //   std::thread([...]()
      //   {
      //       ...
      //   }).detach();
  - blank "//" lines (kept as paragraph breaks)

A line counts as "structured" (left byte-for-byte as-is) if, after removing
one leading space from after the "//", it still starts with whitespace, or
it contains two or more consecutive spaces anywhere (both are strong
signals of manual column alignment). Everything else is treated as normal
prose: consecutive prose lines are joined into a paragraph and re-wrapped
to fill the box width.

Boxes with more than two "//----" lines (e.g. a short reference table,
then a "----" separator, then a prose explanation, then a closing "----")
are handled as multiple segments sharing the same box; each segment is
processed independently and the separator lines themselves are left
untouched.

Usage:
    python3 tools/rewrap_comments.py [--root ROOT] [--dry-run]

By default ROOT is the parent directory of wherever this script lives
(i.e. it assumes the script is at <root>/tools/rewrap_comments.py).
"""

import argparse
import os
import re
import sys
import textwrap

DASH_LINE_RE = re.compile(r'^(?P<indent>[ \t]*)//(?P<dashes>-{10,})[ \t]*$')
COMMENT_LINE_RE = re.compile(r'^(?P<indent>[ \t]*)//(?P<rest>.*)$')


def normalize(raw):
    """Strip exactly one leading space (the normal '// text' prefix space)."""
    return raw[1:] if raw.startswith(' ') else raw


def is_structured(norm):
    """True if this (already-normalized) line should be preserved verbatim."""
    if norm.strip() == '':
        return False  # blank lines are handled separately, not "structured"
    if norm.startswith(' ') or norm.startswith('\t'):
        return True
    if '  ' in norm:
        return True
    return False


def render_segment(indent, raw_lines, wrap_width):
    """Turn a list of raw '//'-stripped content lines into output lines,
    rewrapping prose paragraphs and preserving structured/blank lines."""
    out = []
    paragraph = []

    def flush():
        if not paragraph:
            return
        text = ' '.join(paragraph)
        for wline in (textwrap.wrap(text, width=wrap_width) or ['']):
            out.append(f'{indent}// {wline}')
        paragraph.clear()

    for raw in raw_lines:
        norm = normalize(raw)
        if norm.strip() == '':
            flush()
            out.append(f'{indent}//')
        elif is_structured(norm):
            flush()
            out.append(f'{indent}//{raw}')
        else:
            paragraph.append(norm.strip())
    flush()
    return out


def process_lines(lines):
    """Process a list of source lines. Returns (new_lines, num_boxes_changed)."""
    out = []
    i = 0
    n = len(lines)
    changes = 0

    while i < n:
        m = DASH_LINE_RE.match(lines[i])
        if not m:
            out.append(lines[i])
            i += 1
            continue

        indent = m.group('indent')
        dash_len = len(m.group('dashes'))
        wrap_width = dash_len - 1  # keeps total line length == dash line length

        box_start = i
        dash_lines = [lines[i]]
        segments = []
        current_segment = []
        i += 1

        while i < n:
            dm = DASH_LINE_RE.match(lines[i])
            if dm:
                segments.append(current_segment)
                dash_lines.append(lines[i])
                current_segment = []
                i += 1
                continue
            comm = COMMENT_LINE_RE.match(lines[i])
            if not comm:
                break
            current_segment.append(comm.group('rest'))
            i += 1

        well_formed = len(dash_lines) >= 2 and current_segment == []

        if not well_formed:
            # No proper closing separator; leave the whole run untouched.
            out.extend(lines[box_start:i])
            continue

        for idx, seg in enumerate(segments):
            out.append(dash_lines[idx])
            out.extend(render_segment(indent, seg, wrap_width))
        out.append(dash_lines[-1])
        changes += 1

    return out, changes


def process_file(path, dry_run=False):
    with open(path, 'r', encoding='utf-8') as f:
        original = f.read()

    lines = original.splitlines()
    new_lines, changes = process_lines(lines)
    if changes == 0:
        return 0

    newline = '\r\n' if '\r\n' in original else '\n'
    new_content = newline.join(new_lines)
    if original.endswith('\n') and not new_content.endswith('\n'):
        new_content += newline

    if not dry_run:
        with open(path, 'w', encoding='utf-8', newline='') as f:
            f.write(new_content)

    return changes


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    default_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser.add_argument('--root', default=default_root,
                         help='project root (default: parent directory of this script, i.e. <root>/tools/..)')
    parser.add_argument('--dry-run', action='store_true',
                         help="report what would change without writing any files")
    args = parser.parse_args()

    src_dir = os.path.join(args.root, 'src')
    if not os.path.isdir(src_dir):
        print(f'error: {src_dir} does not exist', file=sys.stderr)
        sys.exit(1)

    total_files = 0
    total_boxes = 0
    for dirpath, _dirnames, filenames in os.walk(src_dir):
        for fn in filenames:
            if fn.endswith('.h') or fn.endswith('.cpp'):
                fpath = os.path.join(dirpath, fn)
                nchg = process_file(fpath, dry_run=args.dry_run)
                if nchg:
                    total_files += 1
                    total_boxes += nchg
                    prefix = '[dry-run] ' if args.dry_run else ''
                    print(f'{prefix}{fpath}: {nchg} box(es) processed')

    print(f'\nDone. {total_boxes} box(es) processed in {total_files} file(s).')


if __name__ == '__main__':
    main()
