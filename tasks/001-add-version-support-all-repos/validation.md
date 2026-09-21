# Task 001 — Validation (Acadia-Instrumentation)

Parent task: `tasks/001-add-version-support-all-repos/validation.md` (Acadia parent repo).

| # | Check | Command | Result |
|---|---|---|---|
| 1 | Version target | `make version` | PASS — prints `1.0.0` |
| 2 | Help lists version | `make help` | PASS — `make version` line present |
| 3 | Regression build+test | `make test` (ctest, host-debug preset) | PASS — 327/327 tests, 0 failed (93.5 s) |
| 4 | Fleet table | parent `make versions` | PASS — Acadia-Instrumentation row shows `1.0.0` |

- Revision: local HEAD + the `Makefile` modification; `VERSION` and CMake
  version generation untouched (verified: target reads `VERSION` directly).
- Skipped checks: none.
- Known limitations: none.