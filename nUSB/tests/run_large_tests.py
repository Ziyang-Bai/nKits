#!/usr/bin/env python3
"""Run real >4 GiB filesystem operations in an explicitly selected scratch directory."""

import argparse
import errno
import os
from pathlib import Path
import shutil
import subprocess

from make_images import large_images


def executable(parser, value):
    path = value.resolve()
    if not path.is_file() or not os.access(path, os.X_OK):
        parser.error("not an executable: %s" % path)
    return path


def sparse_copy(source, destination):
    # Only fixture cloning skips holes. The block-device writer always writes zeroes.
    # Extent seeks change the descriptor position, so both streams stay unbuffered.
    with source.open("rb", buffering=0) as src, destination.open("xb", buffering=0) as dst:
        size = os.fstat(src.fileno()).st_size
        dst.truncate(size)
        offset = 0
        seek_holes = hasattr(os, "SEEK_DATA")
        while offset < size:
            end = size
            if seek_holes:
                try:
                    offset = os.lseek(src.fileno(), offset, os.SEEK_DATA)
                    end = min(size, os.lseek(src.fileno(), offset, os.SEEK_HOLE))
                except OSError as error:
                    if error.errno == errno.ENXIO:
                        break
                    if error.errno not in (errno.EINVAL, errno.ENOTSUP):
                        raise
                    seek_holes = False
            src.seek(offset)
            dst.seek(offset)
            while offset < end:
                data = src.read(min(1024 * 1024, end - offset))
                if not data:
                    raise OSError("short fixture read")
                if data.count(0) != len(data):
                    dst.write(data)
                else:
                    dst.seek(len(data), os.SEEK_CUR)
                offset += len(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workdir", type=Path, required=True)
    parser.add_argument("--mkfs-exfat", type=Path, required=True)
    parser.add_argument("--fsck-exfat", type=Path, required=True)
    parser.add_argument("--driver", type=Path, required=True)
    args = parser.parse_args()
    if not args.workdir.is_absolute():
        parser.error("--workdir must be an explicit absolute scratch path")
    mkfs = executable(parser, args.mkfs_exfat)
    fsck = executable(parser, args.fsck_exfat)
    driver = executable(parser, args.driver)
    workdir = args.workdir.resolve()
    workdir.mkdir(parents=True, exist_ok=True)
    if shutil.disk_usage(workdir).free < 10 << 30:
        parser.error("large test requires at least 10 GiB free in --workdir")
    names = ("large-contiguous.img", "large-fragmented.img")
    for name in names:
        if (workdir / name).exists() or (workdir / (name + ".work")).exists():
            parser.error("scratch images already exist; choose an empty --workdir")
    # Running mkfs on each image checks the tool; some releases return 1 for -V.
    large_images(workdir, mkfs)
    for name in names:
        fixture = workdir / name
        working = workdir / (name + ".work")
        subprocess.run([str(fsck), "-n", str(fixture)], check=True)
        sparse_copy(fixture, working)
        subprocess.run([str(fsck), "-n", str(working)], check=True)
        for phase in ("copy", "move", "mutate", "delete"):
            subprocess.run([str(driver), str(working), phase], check=True)
            subprocess.run([str(fsck), "-n", str(working)], check=True)
        subprocess.run([str(fsck), "-n", str(fixture)], check=True)
    print("Large contiguous and fragmented exFAT tests passed")


if __name__ == "__main__":
    main()
