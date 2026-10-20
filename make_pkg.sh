#!/usr/bin/env bash
#
# Impact - macOS installer package (.pkg).
# Installs to /Library/Audio/Plug-Ins/{VST3,Components,CLAP}.
#
set -euo pipefail
cd "$(dirname "$0")"

VERSION="1.0.3"
PAYLOAD="build/pkg-payload"
OUT="installer/output/Impact-${VERSION}-mac.pkg"

for b in dist/Impact.vst3 dist/Impact.component dist/Impact.clap; do
    if [ ! -d "$b" ]; then
        echo "ERROR: $b not found. Run ./build_mac.sh first." >&2
        exit 1
    fi
done

echo "Preparing package payload..."
rm -rf "$PAYLOAD"
mkdir -p "$PAYLOAD/Library/Audio/Plug-Ins/VST3"
mkdir -p "$PAYLOAD/Library/Audio/Plug-Ins/Components"
mkdir -p "$PAYLOAD/Library/Audio/Plug-Ins/CLAP"
cp -R dist/Impact.vst3      "$PAYLOAD/Library/Audio/Plug-Ins/VST3/"
cp -R dist/Impact.component "$PAYLOAD/Library/Audio/Plug-Ins/Components/"
cp -R dist/Impact.clap      "$PAYLOAD/Library/Audio/Plug-Ins/CLAP/"

mkdir -p installer/output
rm -f "$OUT"

echo "Building installer package..."
pkgbuild --root "$PAYLOAD" \
    --identifier "com.dewpoint.Impact" \
    --version "$VERSION" \
    --install-location "/" \
    "$OUT"

echo
echo "Created: $OUT"
