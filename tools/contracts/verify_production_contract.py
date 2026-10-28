#!/usr/bin/env python3
"""Verify the committed production contract artifacts are reproducible."""

from __future__ import annotations

import hashlib
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
CONTRACTS = ROOT / "contracts"
LOCK = CONTRACTS / "production-contract-lock-v1.json"
COMMAND_AUTHORIZATION_MATRIX = CONTRACTS / "command-authorization-matrix-v1.json"
PROTO_FILES = (
    "common.proto",
    "istate.proto",
    "networkd.proto",
    "monitord.proto",
    "hardware.proto",
    "faults.proto",
    "alarms.proto",
    "logmanager.proto",
    "telemetry.proto",
    "ota.proto",
    "backup.proto",
)


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fail(message: str) -> None:
    print(f"production contract verification failed: {message}", file=sys.stderr)
    raise SystemExit(1)

def verify_command_authorization_matrix() -> None:
    matrix = json.loads(COMMAND_AUTHORIZATION_MATRIX.read_text(encoding="utf-8"))
    if matrix.get("command_identifier", {}).get("field") != "command_id":
        fail("command authorization matrix must define command_id as the shared identifier")
    if set(matrix.get("terminal_states", [])) != {"succeeded", "failed", "expired", "cancelled"}:
        fail("command authorization matrix has an incomplete terminal state model")
    if matrix.get("transitions", {}).get("terminal") != []:
        fail("terminal command states must not transition")
    operations = {row["operation"]: row for row in matrix.get("operations", [])}
    required_confirmations = {
        "backup.restore": "RESTORE",
        "system.factory_reset": "FACTORY-RESET",
        "update.apply": "APPLY-UPDATE",
        "update.rollback": "ROLLBACK",
        "service.control": "SERVICE-CONTROL",
    }
    for operation, confirmation in required_confirmations.items():
        if operations.get(operation, {}).get("confirmation") != confirmation:
            fail(f"command authorization matrix lacks {confirmation} confirmation for {operation}")
    if any(row.get("gateway_actor") != "hq" for row in operations.values()):
        fail("every matrix operation must bind the gateway actor to hq")


def main() -> None:
    lock = json.loads(LOCK.read_text(encoding="utf-8"))
    descriptor = CONTRACTS / lock["descriptor"]["path"]
    if sha256(descriptor) != lock["descriptor"]["sha256"]:
        fail("committed descriptor does not match production-contract-lock-v1.json")

    metadata = json.loads((CONTRACTS / "device-contract-v1.json").read_text(encoding="utf-8"))
    if metadata["proto_descriptor"]["sha256"] != lock["descriptor"]["sha256"]:
        fail("device-contract-v1.json and production lock disagree on descriptor digest")

    for name, digest in lock["configuration_protos"].items():
        if sha256(ROOT / "proto" / name) != digest:
            fail(f"published proto {name} does not match the production lock")

    catalog = CONTRACTS / lock["operation_catalog"]["path"]
    if sha256(catalog) != lock["operation_catalog"]["sha256"]:
        fail("generated operation catalog does not match the production lock")
    verify_command_authorization_matrix()

    subprocess.run(
        [sys.executable, str(ROOT / "tools/catalog/gen_operation_catalog.py"), "--check"],
        check=True,
    )
    subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools/contracts/verify_descriptor_compatibility.py"),
            str(CONTRACTS / "proto-schema-v3-compatibility-baseline.pb"),
            str(descriptor),
        ],
        check=True,
    )
    generated = CONTRACTS / ".proto-schema-v3.verify.pb"
    try:
        subprocess.run(
            [
                "protoc",
                "--include_imports",
                f"--descriptor_set_out={generated}",
                "-I",
                str(ROOT / "proto"),
                *(str(ROOT / "proto" / name) for name in PROTO_FILES),
            ],
            check=True,
        )
        if sha256(generated) != lock["descriptor"]["sha256"]:
            fail("protoc output differs from committed descriptor; regenerate all contract artifacts deliberately")
    finally:
        generated.unlink(missing_ok=True)

    print("production contract is reproducible and locked")


if __name__ == "__main__":
    main()
