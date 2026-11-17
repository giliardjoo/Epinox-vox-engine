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

# One component package per destination. When pkgbuild lists the bundle as relocatable (it does for apps),
# it is set to non relocatable so the bundle always lands in the folder below, even if another copy exists.
make_pkg () {
    local dir="$1" id="$2" location="$3" out="$4"
    local plist="$WORK/$id.plist"
    local args=()
    local i=0

    pkgbuild --analyze --root "$dir" "$plist" > /dev/null

    while /usr/libexec/PlistBuddy -c "Print :$i:BundleIsRelocatable" "$plist" > /dev/null 2>&1; do
        /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist"
        i=$((i + 1))
    done

    if [ "$i" -gt 0 ]; then
        args=(--component-plist "$plist")
    fi

    pkgbuild --root "$dir" \
             ${args[@]+"${args[@]}"} \
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
