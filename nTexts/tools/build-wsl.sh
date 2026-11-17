#!/bin/sh
set -eu

SDK="${NDLESS_SDK:-$HOME/Ndless/ndless-sdk}"
if [ ! -x "$SDK/bin/nspire-gcc" ]; then
    compiler=$(command -v nspire-gcc 2>/dev/null || true)
    if [ -x "${compiler:-}" ]; then
        compiler=$(readlink -f "$compiler")
        SDK=${compiler%/bin/nspire-gcc}
    fi
fi
if [ ! -x "$SDK/bin/nspire-gcc" ]; then
    echo "Ndless SDK not found at: $SDK" >&2
    echo "Set NDLESS_SDK or put nspire-gcc on PATH." >&2
    exit 1
fi

export PATH="$SDK/bin:$SDK/toolchain/install/bin:$PATH"
export USERPROFILE="$HOME"

# Some prebuilt SDK releases pin genzehn to a Boost SONAME older than the
# distro package. Boost.Program_options keeps a stable ABI for this use case;
# provide the expected name privately without touching system libraries.
missing=$(ldd "$SDK/bin/genzehn" 2>/dev/null | awk '/libboost_program_options.*not found/ {print $1; exit}')
if [ -n "${missing:-}" ]; then
    available=$(ldconfig -p | awk '/libboost_program_options\.so\.[0-9]/ {print $NF; exit}')
    if [ -z "${available:-}" ]; then
        echo "genzehn requires Boost.Program_options, but no compatible library is installed." >&2
        exit 1
    fi
    mkdir -p .build/lib
    ln -sf "$available" ".build/lib/$missing"
    export LD_LIBRARY_PATH="$PWD/.build/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

exec make "$@"
