#!/bin/sh
# Makes "Audio Tech Pro <version>.dmg" next to the riff folder: open it and drag the app onto
# Applications, as with Logic. One app for Apple Silicon and Intel Macs (macOS 11 or later).
#   sh scripts/make-mac-dmg.sh
# Uses the app already built by make-mac-pkg.sh if it's there; builds it otherwise.
set -e
cd "$(dirname "$0")/.."

VERSION=$(sed -n 's/^project(AudioTechPro VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
NAME="Audio Tech Pro"
OUT="$(cd .. && pwd)/${NAME} ${VERSION}.dmg"
APP="build-universal/AudioTechPro_artefacts/Release/${NAME}.app"

# 1. The app, for both kinds of Mac (built again so it's up to date: quick when nothing changed)
cmake -B build-universal -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DRIFF_BUILD_TESTS=OFF
cmake --build build-universal --config Release -j "$(sysctl -n hw.ncpu)"
codesign --force --deep --sign - "$APP"

# 2. The disk image: the app and a shortcut to Applications, side by side
WORK="build-dmg"
rm -rf "$WORK"
mkdir -p "$WORK/stage"
ditto "$APP" "$WORK/stage/${NAME}.app"
ln -s /Applications "$WORK/stage/Applications"

hdiutil create -volname "${NAME} ${VERSION}" -srcfolder "$WORK/stage" -fs HFS+ -format UDRW -ov "$WORK/rw.dmg"

# 3. The window as Logic's: big icons, the app on the left, Applications on the right
DEVICE=$(hdiutil attach -readwrite -noverify -noautoopen "$WORK/rw.dmg" | awk '/Apple_HFS/ {print $1}')
sleep 2
osascript <<APPLESCRIPT || echo "(window layout skipped)"
tell application "Finder"
    tell disk "${NAME} ${VERSION}"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {200, 120, 760, 460}
        set opts to the icon view options of container window
        set arrangement of opts to not arranged
        set icon size of opts to 128
        set position of item "${NAME}.app" of container window to {140, 160}
        set position of item "Applications" of container window to {420, 160}
        update without registering applications
        delay 1
        close
    end tell
end tell
APPLESCRIPT
sync
hdiutil detach "$DEVICE" -quiet || hdiutil detach "$DEVICE" -force -quiet

# 4. Compressed and read-only, for sharing
rm -f "$OUT"
hdiutil convert "$WORK/rw.dmg" -format UDZO -imagekey zlib-level=9 -o "$OUT"
rm -rf "$WORK"
echo ""
echo "Made: $OUT"
