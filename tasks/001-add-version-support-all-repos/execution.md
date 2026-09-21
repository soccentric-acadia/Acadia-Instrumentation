# Task 001 — Execution log (Acadia-Instrumentation)

Parent task: `tasks/001-add-version-support-all-repos/execution.md` (Acadia parent repo).

## Approved scope

Add a Makefile `version` target reading the existing `VERSION` file, plus
`.PHONY` and help entries. VERSION/CMake untouched.

## Checklist

- [x] `.PHONY` gains `version`
- [x] `version` target added (`@echo "$$(cat VERSION)"`)
- [x] Help echo block gains version line
- [x] Validate `make version` → `1.0.0`; `make help`; `make test` regression

## Files changed

- `Makefile` (modified — 3 additions, no removals)

## Notes

- First edit accidentally produced `$$$(cat VERSION)` (make-variable expansion
  to empty); immediately detected via `make version` verification and corrected
  to the shell-substitution form `$$(cat VERSION)`. No other deviations.