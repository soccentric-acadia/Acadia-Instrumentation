# Changelog

Maintained by the Loop Engine. One entry per loop that touched this repo,
newest first, appended on loop close (pass or fail) and committed on the loop's
own feature branch. See the master repo's `Arches-Loop-Orchestration-Spec-v2.md` §11.1.

Entries written by hand are fine; the engine inserts directly under this header.

## Yocto recipe moved to meta-acadia

The in-repo `yocto/` layer is removed: the `instrumentation` bitbake recipe now lives in the
OS layer (`Acadia-OS/meta/meta-acadia/recipes-instrumentation/instrumentation/`), and
`acadia-image.bb` installs it. This repo is a plain CMake project again — no bitbake files.
Runtime fixes carried over to the meta-acadia recipe from the RPi 5 audit: scarthgap series
compat, Acadia SRC_URI/SRCREV, parse-time-valid PV, gpio group for the udev rule, and
CM5 machine mapping.

## 1.0.0

First tagged version of the suite. `VERSION` is one version for the whole
suite (REQ §2) — the ten daemons and ten `*_ctrl` tools are released together
and are not independently versioned.
