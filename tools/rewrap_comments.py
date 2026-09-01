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
    python3 tools/rewrap_comments.py [--root ROOT] [--auto] [--log FILE]
    python3 tools/rewrap_comments.py [--auto] [--log FILE] FILE [FILE ...]

With no positional arguments, walks <root>/src and processes every .h/.cpp
file found there. If one or more FILE arguments are given, only those
specific files are processed instead (any extension is accepted in this
mode, and --root is ignored for file selection).

Every box that needed rewrapping -- applied or skipped -- is recorded to
<root>/logs/rewrap_comments.log (created if needed; --log FILE writes
elsewhere instead), the same logs/ convention check_comments.py uses, so
a run's skipped boxes are still on record even when nothing gets written.

Review mode (default, no --auto):
    Each box that actually needs rewrapping is shown as a diff, and the
    script waits for a single keypress: Enter applies that box's change,
    Esc skips it and leaves it untouched. A file is only written if at
    least one box in it was applied.

Automatic mode (--auto):
    Every detected change is applied and every file with changes is
    written, with no prompts at all.

By default ROOT is the parent directory of wherever this script lives
(i.e. it assumes the script is at <root>/tools/rewrap_comments.py).
"""

import argparse
import difflib
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


def find_chunks(lines):
    """Split a list of source lines into chunks:
      ('line', text)                         -- passthrough, non-box content
      ('box', box_start, original, rewrapped) -- a well-formed box, with its
                                                  original lines and its
                                                  rewrapped replacement lines
    box_start is the 0-based index of the box's first line, used for
    reporting during interactive review."""
    chunks = []
    i = 0
    n = len(lines)

    while i < n:
        m = DASH_LINE_RE.match(lines[i])
        if not m:
            chunks.append(('line', lines[i]))
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
            for l in lines[box_start:i]:
                chunks.append(('line', l))
            continue

        original = lines[box_start:i]
        rewrapped = []
        for idx, seg in enumerate(segments):
            rewrapped.append(dash_lines[idx])
            rewrapped.extend(render_segment(indent, seg, wrap_width))
        rewrapped.append(dash_lines[-1])

        chunks.append(('box', box_start, original, rewrapped))

    return chunks


def get_key():
    """Read a single keypress and classify it as 'enter', 'esc', or None
    (anything else). Falls back to line-based input if no raw terminal is
    available (e.g. stdin is piped/redirected)."""
    if not sys.stdin.isatty():
        line = sys.stdin.readline()
        if line == '':  # EOF
            return 'esc'
        return 'enter'

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


def review_box(path, box_start, original, rewrapped):
    """Show a diff for one box and wait for the user to accept (Enter) or
    skip (Esc) it. Returns True to apply the change, False to skip it."""
    print(f'\n----- {path}:{box_start + 1} -----')
    diff = difflib.unified_diff(original, rewrapped, lineterm='')
    for line in diff:
        print(line)
    print('[Enter] apply   [Esc] skip ', end='', flush=True)

    while True:
        key = get_key()
        print()
        if key == 'enter':
            return True
        if key == 'esc':
            return False
        print('[Enter] apply   [Esc] skip ', end='', flush=True)


def process_file(path, auto=False):
    """Process one file.

    auto=True  -- automatic mode: apply every detected change and write
                  the file, with no prompts.
    auto=False -- review mode: show a diff for each box that actually
                  changes and let the user accept/skip it with a
                  keypress; only write the file if something was
                  applied.

    Returns (changes, log_entries). log_entries has one
    (box_start_line_1_based, status) pair for every box that needed
    rewrapping, status being 'applied' or 'skipped' -- skipped boxes are
    included too, so the log is a full record of what still needs a
    second look even when nothing in the file was written.
    """
    with open(path, 'r', encoding='utf-8') as f:
        original_text = f.read()

    lines = original_text.splitlines()
    chunks = find_chunks(lines)

    out = []
    changes = 0
    log_entries = []

    for chunk in chunks:
        if chunk[0] == 'line':
            out.append(chunk[1])
            continue

        _, box_start, box_original, box_rewrapped = chunk

        if box_original == box_rewrapped:
            out.extend(box_original)
            continue

        if auto:
            apply_change = True
        else:
            apply_change = review_box(path, box_start, box_original, box_rewrapped)

        if apply_change:
            out.extend(box_rewrapped)
            changes += 1
            log_entries.append((box_start + 1, 'applied'))
        else:
            out.extend(box_original)
            log_entries.append((box_start + 1, 'skipped'))

    if changes == 0:
        return 0, log_entries

    newline = '\r\n' if '\r\n' in original_text else '\n'
    new_content = newline.join(out)
    if original_text.endswith('\n') and not new_content.endswith('\n'):
        new_content += newline

    with open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(new_content)

    return changes, log_entries


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    default_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser.add_argument('--root', default=default_root,
                         help='project root (default: parent directory of this script, i.e. <root>/tools/..)')
    parser.add_argument('--auto', action='store_true',
                         help="apply all detected changes automatically and write every "
                              "file with changes, without prompting for each box "
                              "(default is to review each box interactively)")
    parser.add_argument('--log', metavar='FILE',
                         help='write the box log to FILE, in addition to stdout '
                              '(default: <root>/logs/rewrap_comments.log -- shared by '
                              'any tool that adopts the same convention)')
    parser.add_argument('files', nargs='*', metavar='FILE',
                         help='one or more specific files to process, instead of '
                              'walking <root>/src. When given, --root is ignored for '
                              'file selection (though the default log path still uses '
                              'it), and any file extension is accepted.')
    args = parser.parse_args()

    tool_stem = os.path.splitext(os.path.basename(__file__))[0]
    log_path = args.log or os.path.join(args.root, 'logs', f'{tool_stem}.log')

    total_files = 0
    total_boxes = 0
    prefix = '[auto] ' if args.auto else ''
    log_lines = []

    def record(fpath, log_entries):
        if not log_entries:
            return
        log_lines.append(f'{fpath}  ({len(log_entries)} box(es))')
        for line_no, status in log_entries:
            log_lines.append(f'  line {line_no:<5} [{status}]')

    if args.files:
        for fpath in args.files:
            if not os.path.isfile(fpath):
                print(f'error: {fpath} does not exist', file=sys.stderr)
                sys.exit(1)
            nchg, log_entries = process_file(fpath, auto=args.auto)
            record(fpath, log_entries)
            if nchg:
                total_files += 1
                total_boxes += nchg
                print(f'{prefix}{fpath}: {nchg} box(es) processed')
    else:
        src_dir = os.path.join(args.root, 'src')
        if not os.path.isdir(src_dir):
            print(f'error: {src_dir} does not exist', file=sys.stderr)
            sys.exit(1)

        for dirpath, _dirnames, filenames in os.walk(src_dir):
            for fn in filenames:
                if fn.endswith('.h') or fn.endswith('.cpp'):
                    fpath = os.path.join(dirpath, fn)
                    nchg, log_entries = process_file(fpath, auto=args.auto)
                    record(fpath, log_entries)
                    if nchg:
                        total_files += 1
                        total_boxes += nchg
                        print(f'{prefix}{fpath}: {nchg} box(es) processed')

    summary = f'Done. {total_boxes} box(es) processed in {total_files} file(s).'
    print(f'\n{summary}')
    log_lines.append(summary)

    os.makedirs(os.path.dirname(os.path.abspath(log_path)), exist_ok=True)
    with open(log_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(log_lines) + '\n')
    print(f'Log written to {log_path}')


if __name__ == '__main__':
    main()