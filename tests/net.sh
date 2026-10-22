#!/bin/sh
# Network path test against tests/fake_bjnp.py (no printer needed):
#   id, direct send (with "busy" answers), and the CUPS relay path.
# Usage: tests/net.sh BUILD_DIR
set -eu

build=$1
out=$build/net
port=18611
rm -rf "$out"
mkdir -p "$out"

python3 tests/fake_bjnp.py --port $port --busy 2 "$out/received" > "$out/fake.log" 2>&1 &
fake=$!
relay=
trap 'kill $fake ${relay:-} 2>/dev/null || true' EXIT
sleep 1

sample=$build/samples/sample_a4_plain_std.bin

echo "== id"
"$build/mx870-bjnp" id 127.0.0.1:$port

echo "== send (printer answers busy twice first)"
"$build/mx870-bjnp" send 127.0.0.1:$port "$sample" "test page"

echo "== relay (as CUPS would use it: socket://127.0.0.1:19870)"
"$build/mx870-bjnp" relay 127.0.0.1:$port 19870 2> "$out/relay.log" &
relay=$!
sleep 1
python3 -c "import socket,sys; s=socket.create_connection(('127.0.0.1',19870)); s.sendall(open(sys.argv[1],'rb').read()); s.close()" "$sample"
sleep 2

cat "$out/fake.log"
for f in "$out"/received/job-*.bj; do
  cmp "$f" "$sample" && echo "$f identical to sample"
  python3 tools/bjdump.py -q "$f" | grep -E 'page\(s\)|INVALID'
done
test "$(ls "$out"/received/job-*.bj | wc -l)" -eq 2
