### What this PR does

One-paragraph summary. Link the issue it closes if there is one.

### Why

Either fix-driven (link the bug, describe the root cause) or scope-driven (point at the README/CONTRIBUTING line that justifies the addition).

### How

Bullet list of the meaningful changes. Group by file or by subsystem, not by commit.

### Test plan

- [ ] `make` succeeds clean from a `make clean` start.
- [ ] `make test` reports `N PASSED, 0 FAILED` (paste the actual line).
- [ ] No new `TODO`/`FIXME`/`XXX`/`HACK` markers — `grep -rn 'TODO\|FIXME\|XXX\|HACK' --include='*.c' --include='*.h' --include='*.asm' src/ include/` returns nothing.
- [ ] If this touches a UI path, manually exercised in `make run-ramfb` and a screenshot is attached.
- [ ] Subsystem `CLAUDE.md` and matching `docs/` section reflect the change.
- [ ] Commit history is one-file-per-commit, lowercase imperative messages.

### Screenshot

For UI changes only. Attach a PPM or PNG from `screendump`.
