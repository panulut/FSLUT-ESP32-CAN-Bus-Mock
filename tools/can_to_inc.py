#!/usr/bin/env python3
"""Strict classic-CAN CSV / supplied adapter TXT -> flash-resident C++ recording."""
from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path
import re
import sys
from file_utils import atomic_write


@dataclass(frozen=True)
class Record:
    timestamp_ms: int
    can_id: int
    dlc: int
    data: tuple[int, ...]
    extended: bool = False
    rtr: bool = False


def integer(value: str, label: str) -> int:
    value = value.strip()
    if not re.fullmatch(r"(?:0[xX][0-9a-fA-F]+|[0-9]+)", value):
        raise ValueError(f"{label}: expected a decimal integer or 0x-prefixed hex")
    return int(value, 16 if value.lower().startswith("0x") else 10)


def boolean(value: str, label: str) -> bool:
    if value.strip().lower() not in {"true", "false", "0", "1"}:
        raise ValueError(f"{label}: expected true/false or 1/0")
    return value.strip().lower() in {"true", "1"}


def payload(value: str) -> tuple[int, ...]:
    tokens = value.split()
    if any(not re.fullmatch(r"(?:0[xX])?[0-9a-fA-F]{2}", t) for t in tokens):
        raise ValueError("data: expected space-separated two-digit hex bytes")
    return tuple(int(t, 16) for t in tokens)


def validate(r: Record) -> None:
    if r.timestamp_ms < 0:
        raise ValueError("timestamp_ms must be nonnegative")
    if not 0 <= r.can_id <= (0x1FFFFFFF if r.extended else 0x7FF):
        raise ValueError("CAN ID exceeds selected standard/extended format")
    if not 0 <= r.dlc <= 8:
        raise ValueError("DLC must be 0..8 (classic CAN only)")
    if len(r.data) != (0 if r.rtr else r.dlc):
        raise ValueError("payload length does not match DLC / RTR")
    if any(not 0 <= byte <= 255 for byte in r.data):
        raise ValueError("payload byte out of range")


TXT_FRAME = re.compile(
    r"\s*(0[xX][0-9A-Fa-f]+)\s+(\d+)\s+\S+\s+\{\s*(.*?)\s*\}\s*"
)
METADATA = (
    "Connected to CAN adapter", "HMS Technology Center", "Hardware/firmware ver",
    "Driver ver", "Serial no", "MessageID", "CAN message polling has stopped",
)


def read_records(path: Path, ignore_nonframes: bool = False,
                 skip_decoded_events: bool = False) -> list[Record]:
    records: list[Record] = []
    with path.open(encoding="utf-8-sig", newline="") as stream:
        if path.suffix.lower() == ".csv":
            reader = csv.DictReader(stream)
            expected = {"timestamp_ms", "can_id", "dlc", "data"}
            if not reader.fieldnames or not expected <= set(reader.fieldnames):
                raise ValueError("CSV needs timestamp_ms,can_id,dlc,data headers")
            if len(set(reader.fieldnames)) != len(reader.fieldnames):
                raise ValueError("duplicate CSV column")
            if set(reader.fieldnames) - expected - {"extended", "rtr"}:
                raise ValueError("unrecognized CSV column")
            for row in reader:
                try:
                    if None in row or any(v is None for v in row.values()):
                        raise ValueError("wrong column count")
                    can_id = integer(row["can_id"], "can_id")
                    r = Record(integer(row["timestamp_ms"], "timestamp_ms"), can_id,
                               integer(row["dlc"], "dlc"), payload(row["data"]),
                               boolean(row["extended"], "extended") if "extended" in row
                               else can_id > 0x7FF,
                               boolean(row["rtr"], "rtr") if "rtr" in row else False)
                    validate(r)
                    records.append(r)
                except ValueError as error:
                    raise ValueError(f"line {reader.line_num}: {error}") from error
        elif path.suffix.lower() == ".txt":
            ignored = 0
            for line_no, line in enumerate(stream, 1):
                stripped = line.strip()
                if not stripped or stripped.startswith("#"):
                    continue
                match = TXT_FRAME.fullmatch(line)
                if not match:
                    if skip_decoded_events and re.fullmatch(
                        r"0x[0-9a-fA-F]+\s+\d+\s+EMCY\s+[^{}]+", stripped
                    ):
                        ignored += 1
                        continue
                    if stripped.startswith(METADATA):
                        continue
                    if ignore_nonframes and not stripped.lower().startswith("0x"):
                        ignored += 1
                        continue
                    raise ValueError(f"line {line_no}: unrecognized or malformed TXT row")
                try:
                    data = payload(match[3])
                    can_id = integer(match[1], "can_id")
                    r = Record(int(match[2]), can_id, len(data), data, can_id > 0x7FF)
                    validate(r)
                    records.append(r)
                except ValueError as error:
                    raise ValueError(f"line {line_no}: {error}") from error
            if ignored:
                print(f"Explicitly skipped {ignored} rows without raw payload", file=sys.stderr)
        else:
            raise ValueError("input extension must be .csv or .txt")
    if not records:
        raise ValueError("input contains no frames")
    previous = records[0].timestamp_ms
    start = previous
    for number, record in enumerate(records, 1):
        if record.timestamp_ms < previous:
            raise ValueError(f"frame {number}: timestamps go backwards; input is never sorted")
        if record.timestamp_ms - start > 0xFFFFFFFF:
            raise ValueError("relative recording duration exceeds uint32 milliseconds")
        previous = record.timestamp_ms
    return records


def render(records: list[Record], loop_gap_ms: int = 1) -> str:
    if not records:
        raise ValueError("no frames")
    if not 1 <= loop_gap_ms <= 0xFFFFFFFF:
        raise ValueError("loop gap must be 1..4294967295 ms")
    lines = ["// Generated by tools/can_to_inc.py. Do not edit.", "#pragma once",
             '#include "can_core.h"', "namespace recording {",
             "inline constexpr cansim::RecordedFrame frames[] = {"]
    start = records[0].timestamp_ms
    for record in records:
        validate(record)
        data = record.data + (0,) * (8 - len(record.data))
        hex_bytes = ", ".join(f"0x{b:02X}" for b in data)
        lines.append(f"    {{{record.timestamp_ms - start}U, {{0x{record.can_id:X}U, "
                     f"{record.dlc}, {{{hex_bytes}}}, "
                     f"{str(record.extended).lower()}, {str(record.rtr).lower()}}}}},")
    lines.extend(["};", "inline constexpr size_t count = sizeof(frames) / sizeof(frames[0]);",
                  f"inline constexpr uint32_t loopGapMs = {loop_gap_ms}U;", "}", ""])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--loop-gap-ms", type=int, default=1,
                        help="time from final frame to next cycle's first frame (default 1)")
    parser.add_argument("--max-frames", type=int, default=100000,
                        help="refuse oversized output rather than silently truncate")
    parser.add_argument("--ignore-nonframes", action="store_true",
                        help="explicitly skip unknown TXT metadata (malformed 0x rows still fail)")
    parser.add_argument("--skip-decoded-events", action="store_true",
                        help="skip decoded EMCY rows whose raw payload is absent; reports count")
    args = parser.parse_args()
    try:
        if args.input.resolve() == args.output.resolve():
            raise ValueError("input and output must differ")
        records = read_records(args.input, args.ignore_nonframes, args.skip_decoded_events)
        if len(records) > args.max_frames:
            raise ValueError(f"{len(records)} frames exceed --max-frames {args.max_frames}")
        output = render(records, args.loop_gap_ms)
        # Validate fully before replacing any previous generated recording.
        atomic_write(args.output, output)
        print(f"Wrote {len(records)} frames, {records[-1].timestamp_ms - records[0].timestamp_ms} ms; "
              f"frame storage approx. {len(records) * 20} flash bytes. Rebuild firmware.")
        return 0
    except (OSError, ValueError, csv.Error) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
