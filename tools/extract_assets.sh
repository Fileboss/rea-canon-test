#!/bin/sh
# Extract the only Canon file the build needs (the MX870 model database,
# cnm_0367.dll) from YOUR copy of Canon's Windows driver installer.
# Nothing from Canon is stored in this repository.
#
# Usage: tools/extract_assets.sh path/to/mp68-win-mx870-1_06-ea24.exe OUTDIR
#
# The installer is a WinZip self-extractor (a ZIP after a small Windows stub),
# which contains Driver/PRNG3.cab (Microsoft Cabinet), which contains the DLL.
# macOS can read both with its own unzip and tar (bsdtar).
set -eu

EXPECTED_SHA256=464d5dcd69c374a25085d85316031dadf3758f11e4f903ce42d971fc55cf7e14

if [ $# -ne 2 ]; then
  echo "usage: $0 path/to/mp68-win-mx870-1_06-ea24.exe OUTDIR" >&2
  exit 2
fi
exe=$1
out=$2

if [ ! -f "$exe" ]; then
  echo "error: $exe not found" >&2
  exit 1
fi

sum=$(shasum -a 256 "$exe" | cut -d' ' -f1)
if [ "$sum" != "$EXPECTED_SHA256" ]; then
  echo "error: $exe is not the expected Canon MX870 driver 1.06 (mp68-win-mx870-1_06-ea24.exe)." >&2
  echo "       SHA-256 $sum" >&2
  echo "       expected $EXPECTED_SHA256" >&2
  echo "       Set MX870_ALLOW_OTHER_EXE=1 to try anyway (tables may differ)." >&2
  [ -n "${MX870_ALLOW_OTHER_EXE:-}" ] || exit 1
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

unzip -q -o "$exe" '*/Driver/PRNG3.cab' -d "$tmp"
cab=$(find "$tmp" -name PRNG3.cab | head -1)
if [ -z "$cab" ]; then
  echo "error: Driver/PRNG3.cab not found in $exe" >&2
  exit 1
fi

mkdir -p "$out"
tar -xf "$cab" -C "$out" cnm_0367.dll
echo "extracted $out/cnm_0367.dll"
