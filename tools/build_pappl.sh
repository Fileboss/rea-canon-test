#!/bin/sh
# Build PAPPL 1.4.9 (https://github.com/michaelrsweet/pappl) as a static arm64
# library into PREFIX. Homebrew has no PAPPL formula; its dependencies come from
# Homebrew: jpeg-turbo, libpng, libusb, openssl@3 (and pkgconf).
#
# Usage: tools/build_pappl.sh PREFIX
set -eu

PAPPL_VERSION=v1.4.9
prefix=$(mkdir -p "$1" && cd "$1" && pwd)
src=$prefix-src

for f in jpeg-turbo libpng libusb openssl@3 pkgconf; do
  if ! brew list --versions "$f" > /dev/null 2>&1; then
    echo "error: Homebrew package $f is missing: brew install jpeg-turbo libpng libusb openssl@3 pkgconf" >&2
    exit 1
  fi
done

if [ -f "$prefix/lib/libpappl.a" ]; then
  echo "PAPPL already built in $prefix"
  exit 0
fi

rm -rf "$src"
git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$PAPPL_VERSION" https://github.com/michaelrsweet/pappl.git "$src"
cd "$src"
PKG_CONFIG_PATH="$(brew --prefix openssl@3)/lib/pkgconfig:$(brew --prefix jpeg-turbo)/lib/pkgconfig:$(brew --prefix libpng)/lib/pkgconfig:$(brew --prefix libusb)/lib/pkgconfig" \
  ./configure --quiet --prefix="$prefix" --disable-shared --with-tls=openssl
# PAPPL's configure targets x86_64+arm64; Homebrew libraries are arm64 only.
sed -i '' 's/-mmacosx-version-min=11.0 -arch x86_64 -arch arm64/-mmacosx-version-min=14.0 -arch arm64/' Makedefs
make -s -j"$(sysctl -n hw.ncpu)" > build.log 2>&1 || { tail -20 build.log; exit 1; }
make -s install > /dev/null
echo "PAPPL $PAPPL_VERSION installed in $prefix"
