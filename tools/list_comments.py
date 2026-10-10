#!/usr/bin/env python3
"""List every comment block in a file, with line numbers and byte size.

The working view for a comment refactor: you edit comments, never code, so you
need the comments and their positions, not the whole file.

    python3 tools/list_comments.py src/foo.cpp
    python3 tools/list_comments.py src/foo.cpp --sizes   # per-block budget
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from cscan import comment_blocks as blocks  # noqa: E402


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    sizes = "--sizes" in sys.argv
    for path in args:
        text = Path(path).read_text()
        total_com = sum(len(t) for _, _, t in blocks(text))
        code = len(text) - total_com
        budget = int(0.08 / 0.92 * code)
        print(f"== {path}: {total_com} comment bytes, budget {budget}")
        for start, end, t in blocks(text):
            head = t.splitlines()[0][:60]
            mark = "!!" if len(t) > budget * 0.25 else "  "
            print(f"{mark} {start:5d}-{end:<5d} {len(t):5d}  {head}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
