# CLAUDE.md

Rules of this project live in `AGENTS.md` — read it before any change. This file
only orients.

## Commands

```bash
cmake --build build -j$(nproc)                    # build build/hyprview.so
cmake --build build --target reload              # rebuild + hot-reload
cd build && ctest --output-on-failure            # 48 checks
hyprctl eval 'hl.plugin.hyprview.cmd("...")'     # live command
python3 tools/runtime_check.py [--list] [lifecycle|scroll|drag|gl]
python3 tools/anim_matrix.py [--quick] [--scenarios]
```

Trace log: `/tmp/hyprview.log` — never the Hyprland log. Success is `CMD|ok`,
failure `CMD|err`.

## Non-negotiable

- Log first, before any edit (AGENTS.md §0). A hypothesis without a log line
  that confirms or refutes it is noise.
- Never `hyprctl --verify-config`; never `rm -rf /run/user/$(id -u)/hypr/*`.
- Comment weight stays under 8% of code: `tests/comment_weight_check.cpp`
  ratchets the measured baseline; growth fails the suite.
- One logical step = one commit, only after a green build. Messages in English.
- Hot-reload target is `reload`. A manual `plugin load` next to a hyprpm copy
  gives two instances.
- Focused commits; judge the build by the date of `build/hyprview.so`, not by
  build output.
