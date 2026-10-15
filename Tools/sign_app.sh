#!/bin/sh
# Signs the app (ad hoc) with a requirement that names its identifier, so macOS keeps its
# privacy permissions (screen recording, microphone) when it's rebuilt or updated.
# Never stops the build: without codesign the app still runs.
APP="$1"
if command -v codesign >/dev/null 2>&1; then
    codesign --force --deep --sign - --requirements '=designated => identifier "com.audiotechpro.app"' "$APP" \
        || echo "warning: couldn't sign $APP (permissions may need allowing again after updates)"
fi
exit 0
