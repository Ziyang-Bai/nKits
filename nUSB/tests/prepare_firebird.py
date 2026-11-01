#!/usr/bin/env python3
"""Copy a v3 Firebird baseline and relocate only its two image paths."""

import argparse
import gzip
import shutil
import struct
from pathlib import Path

HEADER = struct.Struct("<II512s512s")


def image_path(raw):
    end = raw.find(b"\0")
    if end < 0:
        raise ValueError("snapshot path is not NUL terminated")
    path = raw[:end].decode("utf-8")
    if not path or any(ord(char) < 32 for char in path):
        raise ValueError("invalid snapshot image path")
    return path


def prepare(source, dest):
    source = source.resolve(strict=True)
    dest = dest.resolve()
    if dest == source or dest.exists():
        raise ValueError("output directory already exists or equals baseline")
    inputs = [source / name for name in
              ("boot1.bin", "flash.bin", "ndless-ready.snapshot")]
    for path in inputs:
        if not path.is_file():
            raise ValueError("missing baseline file: " + str(path))
    with gzip.open(inputs[2], "rb") as stream:
        original = stream.read()
    if len(original) < HEADER.size:
        raise ValueError("truncated snapshot header")
    sig, version, boot, flash = HEADER.unpack_from(original)
    if sig != 0xCAFEBEE0 or version != 3:
        raise ValueError("expected Firebird snapshot v3")
    image_path(boot)
    image_path(flash)
    paths = [(dest / name).as_posix().encode("utf-8")
             for name in ("boot1.bin", "flash.bin")]
    if any(len(path) >= 512 or b"\0" in path for path in paths):
        raise ValueError("output image path exceeds snapshot field")
    header = HEADER.pack(sig, version, *paths)
    dest.mkdir(parents=True)
    for path in inputs[:2]:
        shutil.copyfile(path, dest / path.name)
    target = dest / inputs[2].name
    with gzip.open(target, "wb") as stream:
        stream.write(header)
        stream.write(memoryview(original)[HEADER.size:])
    with gzip.open(target, "rb") as stream:
        saved = stream.read()
    if saved[:HEADER.size] != header or saved[HEADER.size:] != original[HEADER.size:]:
        raise RuntimeError("relocated snapshot verification failed")
    _, _, boot, flash = HEADER.unpack_from(saved)
    for raw, name in zip((boot, flash), ("boot1.bin", "flash.bin")):
        if Path(image_path(raw)) != dest / name:
            raise RuntimeError("relocated image path verification failed")
    print(target)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        prepare(args.baseline, args.output)
    except (OSError, ValueError, UnicodeError) as exc:
        parser.exit(1, str(exc) + "\n")
