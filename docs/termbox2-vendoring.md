# Vendoring termbox2 instead of linking ncurses

## Context

`dired` links against `ncurses` (`-lncurses`, `#include <ncurses.h>`). This
sandbox only has the runtime package (`libncursesw6`) installed, not the
`-dev` package, so `src/dired.c` fails to compile here:

```
src/dired.c:1:10: fatal error: ncurses.h: No such file or directory
/usr/bin/ld: cannot find -lncurses: No such file or directory
```

## Decision

Rather than depend on a system package being present, vendor a header-only
terminal I/O library so the project builds with just a C compiler — no
`apt install libncurses-dev` step, no system library to find at link time.

`vendor/termbox2.h` was pulled from github.com/termbox/termbox2 at commit
`605398f` (2026-02-07). It's a single-file library (MIT), the stated
lighter-weight alternative to ncurses, with no dependencies beyond libc.
`vendor/termbox2.LICENSE` is the upstream license, kept alongside it.

## Status

Vendored only — not yet wired in. `build.c` still links `-lncurses` and
`src/dired.c` still calls the ncurses API (`initscr`, `getch`, `mvprintw`,
`KEY_UP`, ...).

## Next steps

* `build.c`: drop `-lncurses`; compile `vendor/termbox2.h`'s implementation
  into one translation unit via `#define TB_IMPL`.
* `src/dired.c`: replace the ncurses calls with their termbox2 equivalents
  (`tb_init`/`tb_shutdown`, `tb_poll_event`, `tb_print`, `TB_KEY_ARROW_UP`,
  ...). This is an I/O-layer rewrite, not a drop-in swap.
