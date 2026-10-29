import struct
import sys
import zlib
from pathlib import Path


def read_records(archive_path):
    with open(archive_path, "rb") as handle:
        header = handle.read(24)
        magic, version, type_tag, file_count, name_table_offset = struct.unpack(
            "<4sI4sIQ", header
        )
        if magic != b"BTDX":
            raise ValueError("not a BA2 archive")
        if type_tag != b"GNRL":
            raise ValueError("only GNRL archives are supported")
        if version != 1:
            raise ValueError("only version 1 archives are supported")
        records = []
        for index in range(file_count):
            raw = handle.read(36)
            (
                name_hash,
                ext,
                dir_hash,
                flags,
                offset,
                packed_size,
                unpacked_size,
                align,
            ) = struct.unpack("<I4sIIQIII", raw)
            records.append(
                {
                    "index": index,
                    "ext": ext.decode("ascii", "replace").rstrip("\x00"),
                    "flags": flags,
                    "offset": offset,
                    "packed_size": packed_size,
                    "unpacked_size": unpacked_size,
                    "align": align,
                }
            )
        handle.seek(name_table_offset)
        for record in records:
            length = struct.unpack("<H", handle.read(2))[0]
            record["name"] = handle.read(length).decode("utf-8", "replace")
        return records


def read_payload(archive_path, record):
    with open(archive_path, "rb") as handle:
        handle.seek(record["offset"])
        payload = handle.read(record["packed_size"])
    if record["packed_size"] == 0:
        with open(archive_path, "rb") as handle:
            handle.seek(record["offset"])
            return handle.read(record["unpacked_size"])
    if record["flags"] == 0:
        return payload
    return zlib.decompress(payload)


def main():
    if len(sys.argv) < 2:
        print("usage: ba2inspect.py <archive> [list|extract|grep] [pattern] [outdir]")
        return 2
    archive_path = Path(sys.argv[1])
    command = sys.argv[2] if len(sys.argv) > 2 else "list"
    pattern = sys.argv[3].lower() if len(sys.argv) > 3 else ""
    records = read_records(archive_path)
    if command == "list":
        for record in records:
            print(
                f"{record['unpacked_size']:>10} {record['packed_size']:>10} {record['name']}"
            )
        return 0
    if command == "grep":
        matches = [record for record in records if pattern in record["name"].lower()]
        for record in matches:
            print(
                f"{record['unpacked_size']:>10} {record['packed_size']:>10} {record['name']}"
            )
        print(f"matches: {len(matches)} of {len(records)}")
        return 0
    if command == "extract":
        outdir = Path(sys.argv[4]) if len(sys.argv) > 4 else Path("work/extracted")
        matches = [record for record in records if pattern in record["name"].lower()]
        for record in matches:
            target = outdir / record["name"]
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(read_payload(archive_path, record))
            print(f"extracted {record['name']} ({record['unpacked_size']} bytes)")
        print(f"extracted {len(matches)} file(s) to {outdir}")
        return 0
    print(f"unknown command {command}")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
