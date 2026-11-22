#!/bin/sh
# Assemble a package from already cross-compiled ARM64 artifacts.
# Test packages are the default. Set CARDPUTER_PACKAGE_RELEASE=1 explicitly
# when preparing a release candidate after completing the required checks.
set -eu
umask 022
[ "$#" = 7 ] || { echo 'usage: build-test-deb.sh UI SETTINGS VOICE TEXT BRIDGE MAINTAINER OUTPUT.deb' >&2; exit 2; }
ui=$1; settings=$2; voice=$3; text=$4; bridge=$5; maintainer=$6; output=$7
version=${CARDPUTER_TEST_VERSION:-0.1.0~test1}
case "$version" in ''|*[!A-Za-z0-9.+~:-]*|[!0-9]*) echo 'Invalid test version' >&2; exit 2;; esac
description='Local test build of Cardputer voice input
 Experimental package; not suitable for Store publication.'
if [ "${CARDPUTER_PACKAGE_RELEASE:-0}" = 1 ]; then
    case "$version" in *'~'*|*test*|*TEST*) echo 'Release candidate requires a non-test version' >&2; exit 2;; esac
    description='Cardputer Zero voice input
 Voice transcription and input for supported Cardputer Zero applications.'
fi
case "$maintainer" in *'
'*|'') echo 'Invalid maintainer' >&2; exit 2;; esac
for artifact in "$ui" "$settings" "$voice" "$text" "$bridge"; do
    [ -f "$artifact" ] || { echo "Missing artifact: $artifact" >&2; exit 1; }
    readelf -h "$artifact" | grep -q 'Machine:.*AArch64' || { echo "Not an ARM64 ELF: $artifact" >&2; exit 1; }
done
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project=$(CDPATH= cd -- "$here/../.." && pwd)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT HUP INT TERM
chmod 755 "$stage"
mkdir -p "$stage/DEBIAN" "$stage/usr/lib/voice-input" "$stage/usr/bin" "$stage/usr/lib/voice-input/systemd" "$stage/usr/share/APPLaunch/applications" "$stage/usr/share/APPLaunch/share/images" "$stage/usr/share/doc/voice-input/licenses"
install -m 755 "$ui" "$stage/usr/lib/voice-input/settings-ui"
install -m 755 "$settings" "$stage/usr/lib/voice-input/voice-input-settings"
install -m 755 "$voice" "$stage/usr/lib/voice-input/voice-input"
install -m 755 "$text" "$stage/usr/lib/voice-input/voice-input-text"
install -m 755 "$bridge" "$stage/usr/lib/voice-input/shared_bridge.so"
install -m 755 "$here/setup-user" "$stage/usr/lib/voice-input/setup-user"
install -m 755 "$here/remove-user" "$stage/usr/lib/voice-input/remove-user"
install -m 755 "$here/refresh-user" "$stage/usr/lib/voice-input/refresh-user"
install -m 755 "$here/voice-input-settings-ui" "$stage/usr/bin/voice-input-settings-ui"
install -m 755 "$here/prerm" "$stage/DEBIAN/prerm"
install -m 755 "$here/postinst" "$stage/DEBIAN/postinst"
install -m 644 "$here/voice-input.service" "$here/voice-input-text.service" "$stage/usr/lib/voice-input/systemd/"
install -m 644 "$here/voice-input.desktop" "$stage/usr/share/APPLaunch/applications/"
install -m 644 "$here/voice-input.png" "$stage/usr/share/APPLaunch/share/images/"
install -m 644 "$project/LICENSE" "$stage/usr/share/doc/voice-input/copyright"
install -m 644 "$project/store/ui/LICENSE" "$stage/usr/share/doc/voice-input/licenses/CardputerZero-Template-MIT.txt"
install -m 644 "$project"/licenses/*.txt "$stage/usr/share/doc/voice-input/licenses/"
cat > "$stage/DEBIAN/control" <<CONTROL
Package: voice-input
Version: $version
Architecture: arm64
Maintainer: $maintainer
Section: utils
Priority: optional
Conflicts: cardputer-voice
Depends: applaunch, alsa-utils, curl, pulseaudio-utils, libc6, libstdc++6, libgcc-s1, libfreetype6, libpng16-16, libjpeg62-turbo, zlib1g, libfmt10
Description: $description
CONTROL
dpkg-deb --build -Zgzip -z1 --root-owner-group "$stage" "$output"
