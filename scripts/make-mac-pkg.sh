#!/bin/sh
# Makes an installer for Audio Tech Pro: "Audio Tech Pro <version>.pkg" next to the riff folder.
# Double-click it on any Mac (Apple Silicon or Intel, macOS 11 or later) and it installs the app
# into Applications.
#   sh scripts/make-mac-pkg.sh
# Needs Xcode's command line tools and cmake (the same as building the app).
set -e
cd "$(dirname "$0")/.."

VERSION=$(sed -n 's/^project(AudioTechPro VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
OUT="$(cd .. && pwd)/Audio Tech Pro ${VERSION}.pkg"
WORK="build-pkg"

# 1. The app, for both kinds of Mac, in Release
cmake -B build-universal -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DRIFF_BUILD_TESTS=OFF
cmake --build build-universal --config Release -j "$(sysctl -n hw.ncpu)"
APP="build-universal/AudioTechPro_artefacts/Release/Audio Tech Pro.app"
lipo -info "$APP/Contents/MacOS/Audio Tech Pro"

# 2. Signed for running on this and other Macs (ad hoc: no Apple developer account needed)
codesign --force --deep --sign - "$APP"

# 3. The installer: the app into /Applications
rm -rf "$WORK"
mkdir -p "$WORK/root/Applications" "$WORK/resources"
ditto "$APP" "$WORK/root/Applications/Audio Tech Pro.app"
pkgbuild --analyze --root "$WORK/root" "$WORK/components.plist"
# (always into Applications, even if a copy of the app is somewhere else on the Mac)
plutil -replace 0.BundleIsRelocatable -bool NO "$WORK/components.plist"
pkgbuild --root "$WORK/root" --component-plist "$WORK/components.plist" \
         --identifier com.audiotechpro.app.pkg --version "$VERSION" --install-location / \
         "$WORK/AudioTechPro-component.pkg"

cat > "$WORK/resources/welcome.html" <<EOF
<html><body style="font-family:-apple-system,Helvetica;font-size:13px">
<h2>Audio Tech Pro ${VERSION}</h2>
<p>This installs <b>Audio Tech Pro</b> into your Applications folder.</p>
<p>Runs on Apple Silicon and Intel Macs, macOS 11 or later.</p>
<p>After installing, open it from Applications or Launchpad. The first time, macOS asks for
microphone access: allow it so you can record.</p>
</body></html>
EOF

cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Audio Tech Pro ${VERSION}</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <volume-check><allowed-os-versions><os-version min="11.0"/></allowed-os-versions></volume-check>
    <domains enable_localSystem="true"/>
    <choices-outline><line choice="app"/></choices-outline>
    <choice id="app" title="Audio Tech Pro"><pkg-ref id="com.audiotechpro.app.pkg"/></choice>
    <pkg-ref id="com.audiotechpro.app.pkg" version="${VERSION}" onConclusion="none">AudioTechPro-component.pkg</pkg-ref>
</installer-gui-script>
EOF

rm -f "$OUT"
productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" \
             --package-path "$WORK" "$OUT"
rm -rf "$WORK"
echo ""
echo "Made: $OUT"

# And the uninstaller beside it
sh scripts/make-mac-uninstaller.sh
