#!/bin/sh
# Install the Canon PIXMA MX870 driver for macOS (Apple Silicon).
#
# You bring Canon's own Windows driver installer; this script takes from it the
# tables the driver needs, builds everything, tests it and installs it.
#
#   ./install.sh [options] path/to/mp68-win-mx870-1_06-ea24.exe
#
# Options:
#   --cups            install the classic CUPS driver (default)
#   --app             also build and install the AirPrint application (PAPPL)
#   --printer IP      also set up printing to this network address:
#                       CUPS: BJNP relay (LaunchDaemon) + queue "MX870"
#                       app:  prints the commands to add the printer
#   --no-install      build and test only, install nothing
set -eu

cd "$(dirname "$0")"

usage() {
  sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'
  exit 2
}

want_app=0
printer=
install=1
exe=
while [ $# -gt 0 ]; do
  case $1 in
    --cups) ;;
    --app) want_app=1 ;;
    --printer) [ $# -ge 2 ] || usage; printer=$2; shift ;;
    --no-install) install=0 ;;
    -h|--help) usage ;;
    -*) echo "unknown option $1" >&2; usage ;;
    *) exe=$1 ;;
  esac
  shift
done
[ -n "$exe" ] || usage

step() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
die() { echo "error: $*" >&2; exit 1; }

step "Checking the system"
[ "$(uname -s)" = Darwin ] || die "macOS only"
[ "$(uname -m)" = arm64 ] || echo "warning: built for arm64 (Apple Silicon); this Mac is $(uname -m)"
xcode-select -p > /dev/null 2>&1 || die "Xcode Command Line Tools missing: xcode-select --install"
for tool in cc make python3 unzip tar shasum; do
  command -v $tool > /dev/null || die "$tool not found"
done
if [ $want_app = 1 ]; then
  command -v brew > /dev/null || die "the AirPrint app needs Homebrew (https://brew.sh)"
  missing=
  for f in jpeg-turbo libpng libusb openssl@3 pkgconf; do
    brew list --versions $f > /dev/null 2>&1 || missing="$missing $f"
  done
  [ -z "$missing" ] || die "missing Homebrew packages, run: brew install$missing"
fi
echo "ok"

step "Extracting the tables from your Canon installer"
rm -rf build/assets build/generated
make -s WINEXE="$exe" build/generated/modes.inc build/generated/papers.inc

step "Building"
make -s all
[ $want_app = 0 ] || make -s app

step "Testing (no printer needed)"
make -s check > build/check.log 2>&1 || { tail -20 build/check.log; die "tests failed, see build/check.log"; }
grep -E 'passed|failed' build/check.log

if [ $install = 0 ]; then
  echo
  echo "Built in build/ (not installed)."
  exit 0
fi

step "Installing into /Library/Printers/Canon-MX870 (administrator password)"
sudo make -s install

if [ -n "$printer" ] && [ $want_app = 0 ]; then
  step "Setting up network printing to $printer (CUPS + BJNP relay)"
  /Library/Printers/Canon-MX870/bin/mx870-bjnp id "$printer" || echo "warning: $printer did not answer (BJNP, UDP 8611); continuing"
  plist=/Library/LaunchDaemons/org.mx870.bjnp-relay.plist
  sed "s/PRINTER_IP/$printer/" packaging/org.mx870.bjnp-relay.plist > build/org.mx870.bjnp-relay.plist
  sudo launchctl bootout system "$plist" 2> /dev/null || true
  sudo install -o root -g wheel -m 644 build/org.mx870.bjnp-relay.plist "$plist"
  sudo launchctl bootstrap system "$plist"
  lpadmin -p MX870 -E -v socket://127.0.0.1:9870 \
    -P "/Library/Printers/PPDs/Contents/Resources/Canon MX870 libmx870.ppd.gz"
  echo "Printer \"MX870\" added."
fi

step "Done"
if [ $want_app = 1 ]; then
  cat <<EOF
AirPrint application installed: /Library/Printers/Canon-MX870/bin/mx870-app
Start it (or install packaging/org.mx870.app.plist to start it at login):
  /Library/Printers/Canon-MX870/bin/mx870-app server -o server-port=8870 &
Add the printer:
  /Library/Printers/Canon-MX870/bin/mx870-app add -d MX870 -v bjnp://${printer:-PRINTER_IP} -m canon_mx870
It then appears in System Settings > Printers & Scanners; web page: http://localhost:8870
EOF
elif [ -z "$printer" ]; then
  cat <<EOF
CUPS driver installed. Find the printer:  /Library/Printers/Canon-MX870/bin/mx870-bjnp discover
then rerun with --printer IP to set up network printing, or see README.md.
EOF
fi
