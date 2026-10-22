#!/bin/sh
# End-to-end test of the CUPS driver without a printer:
#   PDF --(macOS cupsfilter + our PPD)--> CUPS raster --(rastertomx870)--> BJ stream
#   --(tools/bjdump.py)--> validated stream + plane images + RGB preview.
#
# Usage: tests/e2e.sh BUILD_DIR INPUT.pdf
set -eu

build=$1
pdf=$2
out=$build/e2e
mkdir -p "$out"

run() {
  name=$1
  shift
  echo "== $name ($*)"
  /usr/sbin/cupsfilter -p "$build/canon-mx870.ppd" -m application/vnd.cups-raster "$@" "$pdf" \
    > "$out/$name.ras" 2> "$out/$name.cupsfilter.log"
  "$build/rastertomx870" 1 user "$name" 1 "" "$out/$name.ras" > "$out/$name.bj" 2> "$out/$name.filter.log"
  grep -E '^(ERROR|DEBUG: page)' "$out/$name.filter.log" || true
  rm -rf "$out/$name.planes"
  python3 tools/bjdump.py -q "$out/$name.bj" --planes "$out/$name.planes" | grep -E 'page\(s\)|INVALID'
  python3 tools/preview.py "$out/$name.planes" 1 "$out/$name.ppm" 8
  sips -s format png "$out/$name.ppm" --out "$out/$name.png" > /dev/null
  rm -f "$out/$name.ras" "$out/$name.ppm"
}

run a4_plain_normal -o PageSize=A4 -o cupsPrintQuality=Normal
run a4_plain_high -o PageSize=A4 -o cupsPrintQuality=High
run letter_draft_gray -o PageSize=Letter -o cupsPrintQuality=Draft -o ColorModel=Gray
run 4x6_photo_borderless -o fit-to-page -o PageSize=4x6.Fullbleed -o MediaType=photographic-glossy -o cupsPrintQuality=High
echo "previews in $out/*.png"
