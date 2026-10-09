#!/bin/sh
# Builds Audio Tech Pro for both Apple Silicon and Intel Macs (one universal app),
# in Release, and zips it next to this folder.
#   sh scripts/build-mac-universal.sh
set -e
cd "$(dirname "$0")/.."
cmake -B build-universal -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DRIFF_BUILD_TESTS=OFF
cmake --build build-universal --config Release -j "$(sysctl -n hw.ncpu)"
APP="build-universal/AudioTechPro_artefacts/Release/Audio Tech Pro.app"
lipo -info "$APP/Contents/MacOS/Audio Tech Pro"
# Ad-hoc signature so it opens on other Macs (right-click > Open the first time).
codesign --force --deep --sign - "$APP"
ditto -c -k --keepParent "$APP" "../Audio Tech Pro (Universal).zip"
echo "Made ../Audio Tech Pro (Universal).zip"
