# Task 001 — Add version support (Acadia-Instrumentation scope)

Parent task: `tasks/001-add-version-support-all-repos/plan.md` (Acadia parent repo).

## Scope (this repository)

- The `VERSION` file already exists (`1.0.0`) and is the single source of truth
  consumed by the top-level CMake project and `cmake/version.h.in`
  (`version_gen.h`, REQ §14). It stays untouched.
- Add to `Makefile`:
  - `.PHONY` gains `version`;
  - a `version` target (CONV-004) printing the VERSION file content:

    ```make
    version: ## Print the project version (CONV-004)
    	@echo "$$(cat VERSION)"
    ```
  - a matching line in the `help` echo block.

## Affected interfaces

- Makefile targets only; CMake build, version generation, contracts, and
  daemons are unaffected.

## Expected files

- `Makefile` (modified)

## Local tests

- `make version` → `1.0.0`
- `make help` → lists `version`
- `make test` → ctest passes (regression; confirms Makefile edit broke nothing)

## Cross-repo role

- Validation-only for the rest: parent `make versions` must show this repo with
  `1.0.0` instead of a failed target printed as `-`.

## Status

Complete (2026-09-21).