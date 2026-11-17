#!/usr/bin/env python3
"""Fingerprint resident code and the libraries selected by the Ndless linker."""

import argparse
import hashlib
import os
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile


def run(command, cwd):
    result = subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    if result.returncode:
        raise RuntimeError("{} failed:\n{}{}".format(
            shlex.join(command), result.stdout, result.stderr))
    return result.stdout + result.stderr


def add_bytes(digest, name, data):
    name = name.encode("utf-8")
    digest.update(struct.pack(">Q", len(name)))
    digest.update(name)
    digest.update(struct.pack(">Q", len(data)))
    digest.update(data)


def add_file(digest, name, path):
    name = name.encode("utf-8")
    digest.update(struct.pack(">Q", len(name)))
    digest.update(name)
    with path.open("rb") as source:
        digest.update(struct.pack(">Q", os.fstat(source.fileno()).st_size))
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)


def link_inputs(root, args):
    # GCC's -### output cannot see the archives injected by Ndless's ld.gold
    # wrapper. Trace a real, disposable link using the same compiler and flags.
    with tempfile.TemporaryDirectory(prefix="nusb-build-id-") as directory:
        directory = Path(directory)
        source = directory / "main.c"
        obj = directory / "main.o"
        elf = directory / "main.elf"
        source.write_text("int main(void) { return 0; }\n", encoding="ascii")
        run(shlex.split(args.cc) + shlex.split(args.cppflags)
            + shlex.split(args.cflags) + ["-c", str(source), "-o", str(obj)], root)
        trace = run(shlex.split(args.ld) + [str(obj), "-o", str(elf)]
                    + shlex.split(args.ldflags) + ["-Wl,-t"], root)
        inputs = set()
        for line in trace.splitlines():
            line = line.strip()
            match = re.fullmatch(r"(.*\.(?:a|o))(?:\([^)]*\))?", line)
            if not match:
                match = re.fullmatch(r"\((.*\.a)\).*", line)
            if not match:
                continue
            path = Path(match.group(1))
            if not path.is_absolute():
                path = root / path
            path = path.resolve()
            if path == obj.resolve():
                continue
            if not path.is_file():
                raise RuntimeError("linker input not found: {}".format(path))
            inputs.add(path)
        archives = {path.name for path in inputs if path.suffix == ".a"}
        if not {"libndls.a", "libnspireio.a"}.issubset(archives) or not (
                {"libsyscalls.a", "libsyscalls_nspireio.a"} & archives):
            raise RuntimeError("link trace did not identify Ndless SDK archives")
        # Hash before the temporary directory disappears; only the probe itself
        # is excluded. Archive names, not installation paths, label the inputs.
        digest = hashlib.sha256()
        for index, path in enumerate(sorted(inputs, key=lambda p: (p.name, str(p)))):
            add_file(digest, "{:04d}/{}".format(index, path.name), path)
        return digest.digest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--ld", required=True)
    parser.add_argument("--extra-source", action="append", default=[])
    for name in ("cppflags", "cflags", "asflags", "ldflags", "zehnflags",
                 "version", "commit", "stage"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    digest = hashlib.sha256()
    add_bytes(digest, "format", b"nusb-resident-build-1")
    for name in ("cc", "ld", "cppflags", "cflags", "asflags", "ldflags",
                 "zehnflags", "version", "commit", "stage"):
        add_bytes(digest, "option/" + name, getattr(args, name).encode("utf-8"))
    add_bytes(digest, "environment/NDLESS_TARGET_CFLAGS",
              os.environ.get("NDLESS_TARGET_CFLAGS", "").encode("utf-8"))
    for name in ("cc", "ld"):
        command = shlex.split(getattr(args, name))
        if not command:
            raise RuntimeError("empty " + name + " command")
        executable = shutil.which(command[0])
        if executable is None:
            raise RuntimeError("tool not found: " + command[0])
        add_file(digest, "tool/" + name, Path(executable).resolve())
        add_bytes(digest, "version/" + name,
                  run(command + ["--version"], root).encode("utf-8"))
    sources = [root.parent / "version.h", root / "Makefile", Path(__file__).resolve()]
    sources.extend(root.parent / "common" / name for name in ("draw.c", "draw.h"))
    sources.extend(root / name for name in args.extra_source)
    for directory in (root / "src", root / "third_party" / "fatfs"):
        if not directory.is_dir():
            raise RuntimeError("source directory not found: " + str(directory))
        sources.extend(path for path in directory.rglob("*")
                       if path.is_file() and path.suffix in (".c", ".h", ".S")
                       and path.name != "resident_build.h")
    for path in sorted(sources, key=lambda p: p.relative_to(root.parent).as_posix()):
        add_file(digest, path.relative_to(root.parent).as_posix(), path)
    add_bytes(digest, "link-inputs", link_inputs(root, args))
    header = ('#ifndef NUSB_RESIDENT_BUILD_H\n#define NUSB_RESIDENT_BUILD_H\n\n'
              '#define NUSB_BUILD_ID "' + digest.hexdigest() + '"\n\n#endif\n')
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists() and output.read_bytes() == header.encode("ascii"):
        return
    with tempfile.NamedTemporaryFile(dir=output.parent, prefix=output.name + ".",
                                     delete=False) as destination:
        temporary = Path(destination.name)
        destination.write(header.encode("ascii"))
    try:
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError) as error:
        sys.exit("build_id: " + str(error))
