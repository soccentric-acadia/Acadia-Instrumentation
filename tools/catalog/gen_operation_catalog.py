#!/usr/bin/env python3
"""Generate contracts/operation-catalog.json from the daemons' command tables.

T-P25 (N04): the single operation catalog. Every spoke registers its control
verbs with ``t.add({"name", "description", {{"arg", required, "desc"}, ...},
handler})`` in ``daemons/<spoke>/src/*.cpp``; this tool parses exactly those
registrations (no hand-kept list) and writes a deterministic JSON catalog
that Acadia-Configuration and Acadia-HQ vendor instead of their hand copies.

    gen_operation_catalog.py            # rewrite contracts/operation-catalog.json
    gen_operation_catalog.py --check    # exit 1 when the committed file drifted
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "contracts" / "operation-catalog.json"
SCHEMA_VERSION = 1

_STR = r'"((?:[^"\\]|\\.)*)"'
# t.add({"name", "description" ["more"...], { args }, [capture]...
ENTRY = re.compile(
    r"t\.add\(\{\s*" + _STR + r"\s*,\s*((?:" + _STR + r"\s*)+),\s*\{(.*?)\}\s*,\s*\[",
    re.DOTALL,
)
ARG = re.compile(r"\{\s*" + _STR + r"\s*,\s*(true|false)\s*,\s*" + _STR + r"\s*\}")
LITERAL = re.compile(_STR)


def _unescape(s: str) -> str:
    return bytes(s, "utf-8").decode("unicode_escape")


def parse(root: pathlib.Path = ROOT) -> dict:
    spokes: dict[str, list[dict]] = {}
    for src in sorted((root / "daemons").glob("*/src/*.cpp")):
        daemon = src.parts[-3]
        text = src.read_text(encoding="utf-8")
        for m in ENTRY.finditer(text):
            name = _unescape(m.group(1))
            description = "".join(_unescape(p) for p in LITERAL.findall(m.group(2)))
            args = [
                {
                    "name": _unescape(a.group(1)),
                    "required": a.group(2) == "true",
                    "description": _unescape(a.group(3)),
                }
                for a in ARG.finditer(m.group(4))
            ]
            spokes.setdefault(daemon, []).append(
                {"name": name, "description": description, "args": args}
            )
    for ops in spokes.values():
        ops.sort(key=lambda o: o["name"])
    return {
        "schema_version": SCHEMA_VERSION,
        "source": "Acadia-Instrumentation daemons/*/src command tables (t.add)",
        "spokes": dict(sorted(spokes.items())),
    }


def render(catalog: dict) -> str:
    return json.dumps(catalog, indent=2, sort_keys=False) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true", help="fail when the committed catalog drifted")
    args = ap.parse_args()
    text = render(parse())
    if args.check:
        current = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
        if current != text:
            print(f"{OUT.relative_to(ROOT)} is stale — run tools/catalog/gen_operation_catalog.py",
                  file=sys.stderr)
            return 1
        print("operation catalog up to date")
        return 0
    OUT.write_text(text, encoding="utf-8")
    total = sum(len(v) for v in parse()["spokes"].values())
    print(f"wrote {OUT.relative_to(ROOT)}: {total} operations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
