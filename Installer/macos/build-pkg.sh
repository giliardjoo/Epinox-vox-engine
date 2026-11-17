#!/bin/bash
# Builds the macOS installer (.pkg) with the VST3, the Audio Unit and the standalone app.
# Usage: build-pkg.sh <version> <artefacts dir> [output dir]
#   <artefacts dir> is build/EpinoxVoxEngine_artefacts/Release
set -euo pipefail

VERSION="${1:?version}"
ART="${2:?artefacts directory}"
OUT="${3:-dist}"
NAME="EPINOX VOX ENGINE"

WORK="$(mktemp -d)"
mkdir -p "$OUT" "$WORK/vst3" "$WORK/au" "$WORK/app"

cp -R "$ART/VST3/$NAME.vst3" "$WORK/vst3/"
cp -R "$ART/AU/$NAME.component" "$WORK/au/"
cp -R "$ART/Standalone/$NAME.app" "$WORK/app/"

# One component package per destination. Bundles are set to non relocatable so they always land in the
# folder below, even if a copy of the app exists somewhere else on the disk.
make_pkg () {
    local dir="$1" id="$2" location="$3" out="$4"
    pkgbuild --analyze --root "$dir" "$WORK/$id.plist" > /dev/null
    /usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$WORK/$id.plist"
    pkgbuild --root "$dir" \
             --component-plist "$WORK/$id.plist" \
             --identifier "$id" \
             --version "$VERSION" \
             --install-location "$location" \
             "$WORK/$out"
}

make_pkg "$WORK/vst3" "com.epinox.voxengine.vst3" "/Library/Audio/Plug-Ins/VST3" "vst3.pkg"
make_pkg "$WORK/au"   "com.epinox.voxengine.au"   "/Library/Audio/Plug-Ins/Components" "au.pkg"
make_pkg "$WORK/app"  "com.epinox.voxengine.app"  "/Applications" "app.pkg"

productbuild --synthesize \
             --package "$WORK/vst3.pkg" --package "$WORK/au.pkg" --package "$WORK/app.pkg" \
             "$WORK/distribution.xml"

productbuild --distribution "$WORK/distribution.xml" \
             --package-path "$WORK" \
             "$OUT/EPINOX-VOX-ENGINE-$VERSION.pkg"

echo "Created $OUT/EPINOX-VOX-ENGINE-$VERSION.pkg"
