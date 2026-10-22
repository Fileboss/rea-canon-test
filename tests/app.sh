#!/bin/sh
# End-to-end test of the PAPPL printer application without a printer:
#   mx870-app server -> printer on bjnp://127.0.0.1:18611 (tests/fake_bjnp.py)
#   -> submit a PNG -> stream received by the simulator -> tools/bjdump.py.
# State, spool and the control socket live under BUILD_DIR/app (HOME/TMPDIR).
# Usage: tests/app.sh BUILD_DIR IMAGE.png
set -eu

build=$(cd "$1" && pwd)
image=$2
out=$build/app
rm -rf "$out"
mkdir -p "$out/home/Library/Application Support" "$out/tmp"
export HOME="$out/home" TMPDIR="$out/tmp"
app="$build/mx870-app"

python3 tests/fake_bjnp.py --port 18611 "$out/received" > "$out/fake.log" 2>&1 &
fake=$!
"$app" server -o server-port=18631 -o log-level=debug -o log-file="$out/server.log" &
server=$!
trap 'kill $fake $server 2>/dev/null || true' EXIT
sleep 2

echo "== drivers"
"$app" drivers
echo "== add printer (bjnp://127.0.0.1:18611)"
"$app" add -d MX870 -v bjnp://127.0.0.1:18611 -m canon_mx870
"$app" printers

echo "== submit A4 color, then 4x6 borderless photo"
"$app" submit -d MX870 -o media=iso_a4_210x297mm -o print-quality=normal "$image"
ipptool -tv -f "$image" ipp://localhost:18631/ipp/print/MX870 tests/borderless-4x6.test | grep -E 'PASS|FAIL|status-code' 

for i in $(seq 1 60); do
  test "$(ls "$out"/received/job-*.bj 2>/dev/null | wc -l)" -ge 2 && break
  sleep 1
done
sleep 1
cat "$out/fake.log"
grep -E 'Page [0-9]+:|ERROR|WARN' "$out/server.log" | sed 's/^.*\] //' | head -20 || true
for f in "$out"/received/job-*.bj; do
  python3 tools/bjdump.py -q "$f" --planes "$f.planes" | grep -E 'page\(s\)|INVALID'
  python3 tools/preview.py "$f.planes" 1 "$f.ppm" 8 && sips -s format png "$f.ppm" --out "$f.png" > /dev/null
done
test "$(ls "$out"/received/job-*.bj | wc -l)" -eq 2
