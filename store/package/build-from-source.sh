#!/bin/sh
# Assemble a package from source using the official CP0 cross SDK.
# Requires the AArch64 Rust target, C/C++ cross compiler, CMake >=3.31,
# Ninja, dpkg-deb, and network access for the pinned LVGL/BSP dependencies.
set -eu
[ "$#" = 2 ] || { echo 'usage: build-from-source.sh "Maintainer <email>" OUTPUT.deb' >&2; exit 2; }
maintainer=$1
case "$2" in /*) output=$2;; *) output=$(pwd)/$2;; esac
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../.." && pwd)
ui="$root/store/ui"
cc=${CARDPUTER_CROSS_CC:-aarch64-linux-gnu-gcc}
command -v "$cc" >/dev/null || { echo 'AArch64 cross compiler not found' >&2; exit 1; }
case "$cc" in */*) PATH="$(dirname "$cc"):$PATH"; export PATH;; esac
export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER="$cc"
cargo build --locked --release --target aarch64-unknown-linux-gnu --manifest-path "$root/rust/Cargo.toml" --bins
set -- --preset cp0-cross -DCMAKE_SKIP_RPATH=ON
[ -z "${CARDPUTER_SDK_ROOT:-}" ] || set -- "$@" "-DCM0_SDK_ROOT=$CARDPUTER_SDK_ROOT" "-DCMAKE_SYSROOT=$CARDPUTER_SDK_ROOT"
[ -z "${CARDPUTER_LVGL_SOURCE:-}" ] || set -- "$@" "-DFETCHCONTENT_SOURCE_DIR_LVGL=$CARDPUTER_LVGL_SOURCE"
[ -z "${CARDPUTER_FMT_INCLUDE:-}" ] || set -- "$@" "-DFMT_INCLUDE_DIR=$CARDPUTER_FMT_INCLUDE"
[ -z "${CARDPUTER_FMT_LIBRARY:-}" ] || set -- "$@" "-DFMT_LIBRARY=$CARDPUTER_FMT_LIBRARY"
(cd "$ui" && cmake "$@" && cmake --build --preset cp0-cross-rel --parallel "${CARDPUTER_BUILD_JOBS:-2}")
bridge=$(mktemp -d)
trap 'rm -rf "$bridge"' EXIT HUP INT TERM
"$cc" -O2 -Wall -Wextra -Werror -fPIC -shared -o "$bridge/shared_bridge.so" "$root/shared_bridge.c" -ldl -pthread
artifacts="$root/rust/target/aarch64-unknown-linux-gnu/release"
"$here/build-test-deb.sh" "$ui/build/cp0-cross/Release/cardputer_voice_settings" "$artifacts/settings" "$artifacts/voice" "$artifacts/text" "$bridge/shared_bridge.so" "$maintainer" "$output"
