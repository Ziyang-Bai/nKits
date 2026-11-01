#!/usr/bin/env python3
"""Identify resident images by their sources, build flags and toolchain."""

import argparse
import hashlib
from pathlib import Path
import shlex
import subprocess


def generate(args):
    digest = hashlib.sha256()
    for value in (args.flags, args.cc):
        digest.update(value.encode())
        digest.update(b"\0")
    command = shlex.split(args.cc)
    digest.update(subprocess.check_output(command + ["--version"]))
    for path in sorted({Path(__file__), *(Path(name) for name in args.sources)}):
        digest.update(path.as_posix().encode())
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    # SDK archives are injected by nspire-ld; include the actual linked copies.
    import tempfile
    with tempfile.TemporaryDirectory(prefix="nkits-build-") as directory:
        source = Path(directory) / "probe.c"
        obj = Path(directory) / "probe.o"
        elf = Path(directory) / "probe.elf"
        source.write_text("int main(void) { return 0; }\n")
        subprocess.run(command + ["-c", str(source), "-o", str(obj)], check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        trace = subprocess.check_output(["nspire-ld", str(obj), "-Wl,-t", "-o", str(elf)],
                                        stderr=subprocess.STDOUT, text=True)
        libraries = sorted({Path(line.strip()) for line in trace.splitlines()
                            if line.strip().endswith(".a") and Path(line.strip()).is_file()})
        if not libraries:
            raise RuntimeError("linker trace contains no SDK archives")
        for path in libraries:
            digest.update(path.as_posix().encode())
            digest.update(path.read_bytes())
    content = '#define NKITS_BUILD_ID "' + digest.hexdigest() + '"\n'
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != content:
        output.write_text(content)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--flags", required=True)
    parser.add_argument("sources", nargs="+")
    generate(parser.parse_args())
