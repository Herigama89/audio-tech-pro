#!/bin/sh
# Makes the installer for Audio Tech Pro Console: "Audio Tech Pro Console <version>.pkg" next to the riff folder.
# Double-click it on any Mac (Apple Silicon or Intel, macOS 11 or later) and it installs the console into Applications.
#   sh scripts/make-mac-console-pkg.sh
# Uses build-universal (made by make-mac-dmg.sh / make-mac-pkg.sh) and builds only the console in it.
set -e
cd "$(dirname "$0")/.."

VERSION=$(sed -n 's/^project(AudioTechPro VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
NAME="Audio Tech Pro Console"
OUT="$(cd .. && pwd)/${NAME} ${VERSION}.pkg"
WORK="build-console-pkg"

# 1. The console, for both kinds of Mac, in Release
cmake -B build-universal -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DRIFF_BUILD_TESTS="${RIFF_BUILD_TESTS:-OFF}"
cmake --build build-universal --config Release --target AudioTechProConsole -j "$(sysctl -n hw.ncpu)"
APP="build-universal/AudioTechProConsole_artefacts/Release/${NAME}.app"
lipo -info "$APP/Contents/MacOS/${NAME}"

DRIVER="build-universal/ConsoleDriver/AudioTechProConsole.driver"
lipo -info "$DRIVER/Contents/MacOS/AudioTechProConsole"

# 2. Signed for running on this and other Macs (ad hoc: no Apple developer account needed)
codesign --force --deep --sign - "$APP"
codesign --force --deep --sign - "$DRIVER"

# 3. The installer: the console into /Applications, its audio device (for your DAW) into Core Audio's drivers
rm -rf "$WORK"
mkdir -p "$WORK/root/Applications" "$WORK/resources" "$WORK/driver/Library/Audio/Plug-Ins/HAL" "$WORK/scripts"
ditto "$APP" "$WORK/root/Applications/${NAME}.app"
ditto "$DRIVER" "$WORK/driver/Library/Audio/Plug-Ins/HAL/AudioTechProConsole.driver"
cat > "$WORK/scripts/postinstall" <<'SH'
#!/bin/sh
# Core Audio loads the new device when it restarts (audio stops for a second)
launchctl kickstart -kp system/com.apple.audio.coreaudiod 2>/dev/null || killall coreaudiod 2>/dev/null
exit 0
SH
chmod +x "$WORK/scripts/postinstall"
pkgbuild --analyze --root "$WORK/driver" "$WORK/driver-components.plist"
plutil -replace 0.BundleIsRelocatable -bool NO "$WORK/driver-components.plist"
pkgbuild --root "$WORK/driver" --component-plist "$WORK/driver-components.plist" \
         --identifier com.audiotechpro.console.driver.pkg --version "$VERSION" \
         --install-location / --scripts "$WORK/scripts" "$WORK/console-driver.pkg"
pkgbuild --analyze --root "$WORK/root" "$WORK/components.plist"
plutil -replace 0.BundleIsRelocatable -bool NO "$WORK/components.plist"
pkgbuild --root "$WORK/root" --component-plist "$WORK/components.plist" \
         --identifier com.audiotechpro.console.pkg --version "$VERSION" --install-location / \
         "$WORK/console-component.pkg"

cat > "$WORK/resources/welcome.html" <<HTML
<html><body style="font-family:-apple-system,Helvetica;font-size:13px">
<h2>${NAME} ${VERSION}</h2>
<p>This installs <b>${NAME}</b> into your Applications folder: a real-time console that runs your
VST3, Audio Unit and CLAP plugins on every input of any audio interface, with headphone mixes and talkback.</p>
<p>Runs on Apple Silicon and Intel Macs, macOS 11 or later.</p>
<p>It also installs the audio device <b>Audio Tech Pro Console</b>: choose it in your DAW to record each
input wet (after its plugins) or dry, and to play your DAW through the console.</p>
<p>The first time, macOS asks for microphone access: allow it so the console can hear your inputs.
Then choose your interface in <b>Audio Setup</b> and scan your plugins in <b>Plugins</b>.</p>
</body></html>
HTML

cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>${NAME} ${VERSION}</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <volume-check><allowed-os-versions><os-version min="11.0"/></allowed-os-versions></volume-check>
    <domains enable_localSystem="true"/>
    <choices-outline><line choice="console"/><line choice="driver"/></choices-outline>
    <choice id="console" title="${NAME}"><pkg-ref id="com.audiotechpro.console.pkg"/></choice>
    <choice id="driver" title="Audio device for your DAW"><pkg-ref id="com.audiotechpro.console.driver.pkg"/></choice>
    <pkg-ref id="com.audiotechpro.console.pkg" version="${VERSION}" onConclusion="none">console-component.pkg</pkg-ref>
    <pkg-ref id="com.audiotechpro.console.driver.pkg" version="${VERSION}" onConclusion="none">console-driver.pkg</pkg-ref>
</installer-gui-script>
XML

rm -f "$OUT"
productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" --package-path "$WORK" "$OUT"
rm -rf "$WORK"
echo ""
echo "Made: $OUT"
