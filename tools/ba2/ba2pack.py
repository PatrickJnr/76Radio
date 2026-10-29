import argparse
import struct
import sys
import zlib
from pathlib import Path

MAGIC = b"BTDX"
TYPE_GNRL = b"GNRL"
VERSION = 1
HEADER_SIZE = 24
RECORD_SIZE = 36
BACKSLASH = chr(92)
ENTRY_FLAGS = 0x100100
ENTRY_ALIGN = 0xBAADF00D

TABLE = []
for index in range(256):
    value = index
    for _ in range(8):
        value = (value >> 1) ^ 0xEDB88320 if value & 1 else value >> 1
    TABLE.append(value)


def normalize_separators(name):
    return name.replace("/", BACKSLASH)


def split_name(name):
    normalized = normalize_separators(name)
    directory, _, filename = normalized.rpartition(BACKSLASH)
    stem, dot, extension = filename.rpartition(".")
    if not dot:
        stem, extension = filename, ""
    return directory, stem, extension


def fo4_hash(text):
    result = 0
    for character in text:
        code = ord(character)
        if code > 127:
            continue
        if character == "/":
            code = ord(BACKSLASH)
        if 65 <= code <= 90:
            code += 32
        result = ((result >> 8) ^ TABLE[(result ^ code) & 0xFF]) & 0xFFFFFFFF
    return result


def read_records(archive_path):
    with open(archive_path, "rb") as handle:
        magic, version, type_tag, count, name_table = struct.unpack(
            "<4sI4sIQ", handle.read(HEADER_SIZE)
        )
        if magic != MAGIC or type_tag != TYPE_GNRL or version != VERSION:
            raise ValueError("unsupported archive")
        records = []
        for _ in range(count):
            (
                name_hash,
                extension,
                directory_hash,
                flags,
                offset,
                packed,
                unpacked,
                align,
            ) = struct.unpack("<I4sIIQIII", handle.read(RECORD_SIZE))
            records.append(
                {
                    "name_hash": name_hash,
                    "extension": extension.decode("ascii", "replace").rstrip(chr(0)),
                    "directory_hash": directory_hash,
                    "flags": flags,
                    "offset": offset,
                    "packed": packed,
                    "unpacked": unpacked,
                    "align": align,
                }
            )
        handle.seek(name_table)
        for record in records:
            length = struct.unpack("<H", handle.read(2))[0]
            record["name"] = normalize_separators(
                handle.read(length).decode("utf-8", "replace")
            )
    return records


def read_payload(archive_path, record):
    with open(archive_path, "rb") as handle:
        handle.seek(record["offset"])
        if record["packed"] == 0:
            return handle.read(record["unpacked"])
        data = handle.read(record["packed"])
    return zlib.decompress(data)


def verify_hashes(archive_path):
    records = read_records(archive_path)
    bad = []
    for record in records:
        directory, stem, extension = split_name(record["name"])
        if fo4_hash(stem) != record["name_hash"]:
            bad.append((record["name"], "name"))
        if fo4_hash(directory) != record["directory_hash"]:
            bad.append((record["name"], "directory"))
        if record["extension"].lower() != extension.lower()[:4]:
            bad.append((record["name"], "extension"))
    return records, bad


def collect_files(root, include):
    root_path = Path(root)
    selected = []
    for path in sorted(root_path.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(root_path).as_posix()
        if include and not any(
            pattern.lower() in relative.lower() for pattern in include
        ):
            continue
        selected.append((relative, path))
    return selected


def pack(root, output, include):
    files = collect_files(root, include)
    if not files:
        raise ValueError("no files selected")
    entries = []
    payload = bytearray()
    name_table = bytearray()
    offset = HEADER_SIZE + RECORD_SIZE * len(files)
    for relative, path in files:
        data = path.read_bytes()
        directory, stem, extension = split_name(relative)
        entries.append(
            {
                "name_hash": fo4_hash(stem),
                "extension": extension[:4].encode("ascii", "replace").ljust(4, b"\x00"),
                "directory_hash": fo4_hash(directory),
                "offset": offset,
                "unpacked": len(data),
                "name": normalize_separators(relative),
            }
        )
        payload.extend(data)
        offset += len(data)
        encoded = normalize_separators(relative).encode("utf-8")
        name_table.extend(struct.pack("<H", len(encoded)))
        name_table.extend(encoded)
    name_table_offset = offset
    with open(output, "wb") as handle:
        handle.write(
            struct.pack(
                "<4sI4sIQ", MAGIC, VERSION, TYPE_GNRL, len(files), name_table_offset
            )
        )
        for entry in entries:
            handle.write(
                struct.pack(
                    "<I4sIIQIII",
                    entry["name_hash"],
                    entry["extension"],
                    entry["directory_hash"],
                    ENTRY_FLAGS,
                    entry["offset"],
                    0,
                    entry["unpacked"],
                    ENTRY_ALIGN,
                )
            )
        handle.write(payload)
        handle.write(name_table)
    for entry in entries:
        print(f"packed {entry['name']} ({entry['unpacked']} bytes)")
    print(f"wrote {output} with {len(files)} file(s)")
    return 0


def verify_round_trip(output, root, include):
    records, bad = verify_hashes(output)
    if bad:
        for name, field in bad:
            print(f"hash mismatch: {name} ({field})")
        return 1
    root_path = Path(root)
    for record in records:
        local = root_path / record["name"].replace(BACKSLASH, "/")
        if not local.exists():
            print(f"missing source file for {record['name']}")
            return 1
        if read_payload(output, record) != local.read_bytes():
            print(f"payload mismatch for {record['name']}")
            return 1
    print(f"round trip verified for {len(records)} file(s)")
    return 0


def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    verify_parser = subparsers.add_parser("verify")
    verify_parser.add_argument("archive")
    list_parser = subparsers.add_parser("list")
    list_parser.add_argument("archive")
    pack_parser = subparsers.add_parser("pack")
    pack_parser.add_argument("root")
    pack_parser.add_argument("output")
    pack_parser.add_argument("--include", action="append", default=[])
    pack_parser.add_argument("--verify-root")
    arguments = parser.parse_args()
    if arguments.command == "verify":
        records, bad = verify_hashes(arguments.archive)
        for name, field in bad:
            print(f"hash mismatch: {name} ({field})")
        print(f"verified {len(records) - len(bad)} of {len(records)} entries")
        return 0 if not bad else 1
    if arguments.command == "list":
        for record in read_records(arguments.archive):
            print(f"{record['unpacked']:>10} {record['packed']:>10} {record['name']}")
        return 0
    result = pack(arguments.root, arguments.output, arguments.include)
    if result != 0:
        return result
    if arguments.verify_root:
        return verify_round_trip(
            arguments.output, arguments.verify_root, arguments.include
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
