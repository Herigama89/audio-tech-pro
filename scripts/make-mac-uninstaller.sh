#!/bin/sh
# Makes "Uninstall Audio Tech Pro.pkg" next to the riff folder (takes a few seconds; nothing is compiled).
# Double-click it on a Mac to remove Audio Tech Pro: the app, its settings, autosaves and themes.
# Your music stays: projects, recordings, patches and templates (Music > Audio Tech Pro).
#   sh scripts/make-mac-uninstaller.sh
set -e
cd "$(dirname "$0")/.."

OUT="$(cd .. && pwd)/Uninstall Audio Tech Pro.pkg"
WORK="build-uninstaller"
rm -rf "$WORK"
mkdir -p "$WORK/scripts" "$WORK/resources"
cp scripts/uninstaller/postinstall "$WORK/scripts/postinstall"
chmod +x "$WORK/scripts/postinstall"

pkgbuild --nopayload --scripts "$WORK/scripts" --identifier com.audiotechpro.uninstall.pkg --version 1.0 \
         "$WORK/uninstall-component.pkg"

cat > "$WORK/resources/welcome.html" <<'HTML'
<html><body style="font-family:-apple-system,Helvetica;font-size:13px">
<h2>Uninstall Audio Tech Pro</h2>
<p>This removes <b>Audio Tech Pro</b> from this Mac:</p>
<ul><li>the app in Applications</li><li>its settings, autosaves and themes</li></ul>
<p><b>Your music is kept:</b> projects, recordings, patches and templates in
Music &gt; Audio Tech Pro, and screen recordings in Movies &gt; Audio Tech Pro.
Delete those yourself if you don't want them.</p>
<p>If Audio Tech Pro is open, save your work first: it will be closed.</p>
</body></html>
HTML

cat > "$WORK/distribution.xml" <<'XML'
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Uninstall Audio Tech Pro</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <options customize="never" require-scripts="true" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <choices-outline><line choice="uninstall"/></choices-outline>
    <choice id="uninstall" title="Uninstall Audio Tech Pro"><pkg-ref id="com.audiotechpro.uninstall.pkg"/></choice>
    <pkg-ref id="com.audiotechpro.uninstall.pkg" version="1.0" onConclusion="none">uninstall-component.pkg</pkg-ref>
</installer-gui-script>
XML

rm -f "$OUT"
productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" --package-path "$WORK" "$OUT"
rm -rf "$WORK"
echo "Made: $OUT"
