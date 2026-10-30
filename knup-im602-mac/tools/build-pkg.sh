#!/bin/bash
# Gera o instalador .pkg (precisa rodar em um Mac; o GitHub Actions faz isso sozinho).
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="${VERSION:-1.0.0}"
ID="cc.jvpereira.knup-im602"
OUT="build"
ROOT="$OUT/root"

rm -rf "$OUT"; mkdir -p "$ROOT/Library/Printers/Knup/Filter" \
                        "$ROOT/Library/Printers/PPDs/Contents/Resources"

echo "== Compilando (Apple Silicon + Intel)"
clang -O2 -Wall -Wno-deprecated-declarations \
  -arch arm64 -arch x86_64 -mmacosx-version-min=11.0 \
  -o "$ROOT/Library/Printers/Knup/Filter/rastertoim602" src/rastertoim602.c -lcups
lipo -info "$ROOT/Library/Printers/Knup/Filter/rastertoim602"

cp ppd/Knup-IM602.ppd "$ROOT/Library/Printers/PPDs/Contents/Resources/"
cp pkg/payload-scripts/*.sh "$ROOT/Library/Printers/Knup/"
chmod 755 "$ROOT/Library/Printers/Knup/"*.sh "$ROOT/Library/Printers/Knup/Filter/rastertoim602"

echo "== Montando pacote"
pkgbuild --root "$ROOT" --identifier "$ID" --version "$VERSION" \
  --scripts pkg/scripts --ownership recommended --install-location / \
  "$OUT/knup-im602-component.pkg"

sed "s/@VERSION@/$VERSION/g" pkg/distribution.xml > "$OUT/distribution.xml"
productbuild --distribution "$OUT/distribution.xml" --resources pkg/resources \
  --package-path "$OUT" "$OUT/Knup-IM602-Mac-$VERSION.pkg"

rm -f "$OUT/knup-im602-component.pkg"
echo "== Pronto: $OUT/Knup-IM602-Mac-$VERSION.pkg"
