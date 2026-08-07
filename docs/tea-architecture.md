# TEA-inspired split: update / view

## Idea

Wire dired's event loop the way The Elm Architecture wires a program:
`main()` owns everything impure (reading input, the terminal, the
filesystem) and delegates the actual decision-making to two pure functions
it can unit test in isolation, without a terminal or a real filesystem.

```
main loop:
  raw input -> Msg (enum)
  model' = update(msg, model)      -- pure
  text   = view(model')            -- pure, renders Model to text
  main prints/draws `text`
```

* `update(Msg, Model) -> Model`: pure, no I/O — no ncurses/termbox calls, no
  `fopen`/`rename`/`mkdir`. Same shape as the tier-2 `AppState` refactor
  already called out in `docs/TESTING.md` (`start_edit`/`cancel_edit`/
  `validate_edit` are already a de facto state machine over `mode`,
  `edit_buf`, `virtual_line`, `selected`).
* `view(Model) -> Text`: pure too — turns a `Model` into whatever the
  renderer needs (rows/lines/cells), with no direct `mvprintw`/`tb_print`
  calls inside it. `main()` is the only thing that ever writes to the
  terminal.
* Both `update` and `view` become plain functions: feed them a `Model`,
  assert on what comes back. No mocking, no ncurses/termbox needed in
  `test/`.

## Open question: commands (effects)

`update` will want to trigger things that are not pure: load a directory's
contents, rename/delete a file, spawn `vim`. Elm solves this with
`Cmd Msg` — `update` returns *both* a new `Model` and a description of an
effect to perform; the runtime (outside the pure core) executes the effect
and feeds the result back in as another `Msg`.

dired needs the same kind of seam: a way for `update` to say "load this
path" or "rename this file" without doing it itself, so `main()` (the
impure shell) is the one that actually touches the filesystem. The shape of
that mechanism — a `Cmd` enum/struct returned alongside `Model`? a queue
`main` drains after each `update` call? something else? — is still open.

## Status

Design idea only, nothing implemented yet. The commands question above is
being worked through with the user before any refactor starts.
