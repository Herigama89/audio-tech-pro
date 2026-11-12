#!/bin/sh
# Packages the plug-ins built by RIFF_BUILD_PLUGINS (build-plugins) into out-plugins/:
#   "Audio Tech Pro Plugins <version>.pkg"  installs every VST3 into /Library/Audio/Plug-Ins/VST3 and every
#                                           Audio Unit into /Library/Audio/Plug-Ins/Components
#   "Audio Tech Pro Plugins <version>.zip"  the same bundles, to copy by hand
set -e
cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/^project(AudioTechPro VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
NAME="Audio Tech Pro Plugins ${VERSION}"
STAGE="build-plugins/stage"
rm -rf "$STAGE" out-plugins
mkdir -p "$STAGE/Library/Audio/Plug-Ins/VST3" "$STAGE/Library/Audio/Plug-Ins/Components" out-plugins

find build-plugins -maxdepth 4 -type d -path '*_artefacts/Release/VST3/*.vst3' | while read -r b; do ditto "$b" "$STAGE/Library/Audio/Plug-Ins/VST3/$(basename "$b")"; done
find build-plugins -maxdepth 4 -type d -path '*_artefacts/Release/AU/*.component' | while read -r b; do ditto "$b" "$STAGE/Library/Audio/Plug-Ins/Components/$(basename "$b")"; done
# (signed for this Mac's own use: Apple Silicon won't load unsigned code)
for b in "$STAGE"/Library/Audio/Plug-Ins/*/*; do codesign --force --deep --sign - "$b"; done
ls -la "$STAGE/Library/Audio/Plug-Ins/VST3" "$STAGE/Library/Audio/Plug-Ins/Components"

pkgbuild --root "$STAGE" --identifier com.audiotechpro.plugins --version "$VERSION" --install-location / "out-plugins/${NAME}.pkg"
(cd "$STAGE/Library/Audio/Plug-Ins" && zip -qry "$(cd ../../../../.. && pwd)/out-plugins/${NAME}.zip" VST3 Components)
ls -la out-plugins
