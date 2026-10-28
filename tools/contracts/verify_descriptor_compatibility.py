#!/usr/bin/env python3
"""Reject wire-incompatible protobuf changes against a frozen descriptor."""

from __future__ import annotations

import argparse
import pathlib
import sys


def wire_fields(data: bytes) -> list[tuple[int, int | bytes]]:
    fields: list[tuple[int, int | bytes]] = []
    offset = 0
    while offset < len(data):
        tag, offset = read_varint(data, offset)
        field, wire_type = tag >> 3, tag & 7
        if wire_type == 0:
            value, offset = read_varint(data, offset)
        elif wire_type == 2:
            length, offset = read_varint(data, offset)
            value, offset = data[offset : offset + length], offset + length
        else:
            raise ValueError(f"unsupported protobuf wire type {wire_type}")
        fields.append((field, value))
    return fields


def read_varint(data: bytes, offset: int) -> tuple[int, int]:
    value, shift = 0, 0
    while True:
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return value, offset
        shift += 7


def strings(fields: list[tuple[int, int | bytes]], number: int) -> list[str]:
    return [value.decode() for field, value in fields if field == number and isinstance(value, bytes)]


def messages(data: bytes, prefix: str = "") -> dict[str, dict[int, tuple[str, int, str, int]]]:
    result: dict[str, dict[int, tuple[str, int, str, int]]] = {}
    for field, value in wire_fields(data):
        if field != 4 or not isinstance(value, bytes):
            continue
        message = wire_fields(value)
        name = strings(message, 1)[0]
        qualified = f"{prefix}.{name}" if prefix else name
        result[qualified] = {
            int(number): (strings(fields, 1)[0], int(strings(fields, 5)[0]) if False else int(next(value for field, value in fields if field == 5)), strings(fields, 6)[0] if strings(fields, 6) else "", int(next(value for field, value in fields if field == 4)))
            for field, fields_value in message
            if field == 2 and isinstance(fields_value, bytes)
            for fields in [wire_fields(fields_value)]
            for number in [next(value for field, value in fields if field == 3)]
        }
        result.update(nested_messages(value, qualified))
    return result


def nested_messages(data: bytes, prefix: str) -> dict[str, dict[int, tuple[str, int, str, int]]]:
    result: dict[str, dict[int, tuple[str, int, str, int]]] = {}
    for field, value in wire_fields(data):
        if field != 3 or not isinstance(value, bytes):
            continue
        message = wire_fields(value)
        name = strings(message, 1)[0]
        qualified = f"{prefix}.{name}"
        result[qualified] = {
            int(number): (strings(fields, 1)[0], int(next(value for field, value in fields if field == 5)), strings(fields, 6)[0] if strings(fields, 6) else "", int(next(value for field, value in fields if field == 4)))
            for field, fields_value in message
            if field == 2 and isinstance(fields_value, bytes)
            for fields in [wire_fields(fields_value)]
            for number in [next(value for field, value in fields if field == 3)]
        }
        result.update(nested_messages(value, qualified))
    return result


def descriptor_index(data: bytes) -> tuple[dict[str, dict[int, tuple[str, int, str, int]]], dict[str, dict[str, tuple[str, str, int, int]]]]:
    all_messages: dict[str, dict[int, tuple[str, int, str, int]]] = {}
    all_services: dict[str, dict[str, tuple[str, str, int, int]]] = {}
    for field, value in wire_fields(data):
        if field != 1 or not isinstance(value, bytes):
            continue
        file = wire_fields(value)
        package = strings(file, 2)
        package_prefix = package[0] if package else ""
        all_messages.update({f"{package_prefix}.{name}" if package_prefix else name: message for name, message in messages(value).items()})
        for service_field, service_value in file:
            if service_field != 6 or not isinstance(service_value, bytes):
                continue
            service = wire_fields(service_value)
            service_name = strings(service, 1)[0]
            methods: dict[str, tuple[str, str, int, int]] = {}
            for method_field, method_value in service:
                if method_field == 2 and isinstance(method_value, bytes):
                    method = wire_fields(method_value)
                    methods[strings(method, 1)[0]] = (
                        strings(method, 2)[0],
                        strings(method, 3)[0],
                        int(next((value for field, value in method if field == 5), 0)),
                        int(next((value for field, value in method if field == 6), 0)),
                    )
            qualified = f"{package_prefix}.{service_name}" if package_prefix else service_name
            all_services[qualified] = methods
    return all_messages, all_services


def compatibility_errors(baseline: bytes, current: bytes) -> list[str]:
    old_messages, old_services = descriptor_index(baseline)
    new_messages, new_services = descriptor_index(current)
    return compatibility_errors_for_indexes(old_messages, old_services, new_messages, new_services)


def compatibility_errors_for_indexes(
    old_messages: dict[str, dict[int, tuple[str, int, str, int]]],
    old_services: dict[str, dict[str, tuple[str, str, int, int]]],
    new_messages: dict[str, dict[int, tuple[str, int, str, int]]],
    new_services: dict[str, dict[str, tuple[str, str, int, int]]],
) -> list[str]:
    errors: list[str] = []
    for name, old_fields in old_messages.items():
        new_fields = new_messages.get(name)
        if new_fields is None:
            errors.append(f"removed message: {name}")
            continue
        for number, old_field in old_fields.items():
            if number not in new_fields:
                errors.append(f"removed field: {name}.{old_field[0]} ({number})")
            elif old_field != new_fields[number]:
                errors.append(f"changed field: {name}.{old_field[0]} ({number})")
    for name, old_methods in old_services.items():
        new_methods = new_services.get(name)
        if new_methods is None:
            errors.append(f"removed service: {name}")
            continue
        for method_name, old_method in old_methods.items():
            if method_name not in new_methods:
                errors.append(f"removed method: {name}.{method_name}")
            elif old_method != new_methods[method_name]:
                errors.append(f"changed method: {name}.{method_name}")
    return errors


def self_test() -> int:
    baseline = pathlib.Path(__file__).resolve().parents[2] / "contracts/proto-schema-v3-compatibility-baseline.pb"
    data = baseline.read_bytes()
    if compatibility_errors(data, data):
        print("self-test failed: an identical descriptor was rejected", file=sys.stderr)
        return 1
    messages_index, services_index = descriptor_index(data)
    message_name, fields = next((name, fields) for name, fields in messages_index.items() if fields)
    service_name, methods = next((name, methods) for name, methods in services_index.items() if methods)
    current_messages = {name: dict(fields) for name, fields in messages_index.items()}
    field_number = next(iter(fields))
    field = current_messages[message_name][field_number]
    current_messages[message_name][field_number] = (field[0], field[1] + 1, field[2], field[3])
    current_services = {name: dict(methods) for name, methods in services_index.items()}
    del current_services[service_name][next(iter(methods))]
    errors = compatibility_errors_for_indexes(messages_index, services_index, current_messages, current_services)
    without_field = {name: dict(fields) for name, fields in messages_index.items()}
    del without_field[message_name][field_number]
    errors.extend(compatibility_errors_for_indexes(messages_index, services_index, without_field, services_index))
    without_service = dict(current_services)
    del without_service[service_name]
    errors.extend(compatibility_errors_for_indexes(messages_index, services_index, messages_index, without_service))
    if (
        not any("changed field" in error for error in errors)
        or not any("removed field" in error for error in errors)
        or not any("removed method" in error for error in errors)
        or not any("removed service" in error for error in errors)
    ):
        print("self-test failed: incompatible changes were not rejected", file=sys.stderr)
        return 1
    print("descriptor compatibility verifier self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=pathlib.Path, nargs="?")
    parser.add_argument("current", type=pathlib.Path, nargs="?")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.baseline is None or args.current is None:
        parser.error("baseline and current descriptors are required")
    errors = compatibility_errors(args.baseline.read_bytes(), args.current.read_bytes())
    if errors:
        print("descriptor compatibility verification failed:", file=sys.stderr)
        print("\n".join(f"- {error}" for error in errors), file=sys.stderr)
        return 1
    print("descriptor compatibility verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
