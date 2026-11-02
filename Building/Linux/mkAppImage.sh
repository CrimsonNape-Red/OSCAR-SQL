#!/bin/bash
# Build an OSCAR AppImage from a release build (Qt 6), next to mkOSDistDeb-Qt6.sh.
#
#   OSCAR_BUILD_DIRECTORY=build-pkg bash ./mkAppImage.sh
#
# OSCAR_BUILD_DIRECTORY is relative to the folder above OSCAR-code, as for the .deb
# script, and must hold a release build (qmake6 OSCAR_QT.pro CONFIG+=release).
#
# The tools (linuxdeploy, its Qt plugin, appimagetool) are pinned to the versions below.
# They are taken from OSCAR_APPIMAGE_TOOLS (default ~/.cache/oscar-appimage-tools) and
# downloaded there, checksum checked, if missing.
#
# An AppImage runs on distributions at least as new as the one it was built on (glibc).
# It can't install the udev rule for the Viatom/Wellue USB cable as the .deb does; OSCAR
# offers to install it itself ("Allow OSCAR to use the cable...") when it finds a cable
# it may not open.

set -euo pipefail

LINUXDEPLOY_VERSION="1-alpha-20251107-1"
LINUXDEPLOY_SHA256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
QTPLUGIN_VERSION="1-alpha-20250213-1"
QTPLUGIN_SHA256="15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724"
APPIMAGETOOL_VERSION="1.9.1"
APPIMAGETOOL_SHA256="ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"

here=$(cd "$(dirname "$0")" && pwd)
SRC=${here%/*/*}/oscar
build_folder=${here%/*/*/*}/${OSCAR_BUILD_DIRECTORY:-build}
tools=${OSCAR_APPIMAGE_TOOLS:-$HOME/.cache/oscar-appimage-tools}

VERSION=$(awk '/#define VERSION / { gsub(/"/, "", $3); print $3 }' "$SRC/VERSION")
echo "Version: $VERSION"
echo "Build directory: $build_folder"
binary="$build_folder/oscar/OSCAR20"
if [ ! -x "$binary" ]; then
    echo "No OSCAR20 in $build_folder/oscar: build it first (qmake6 OSCAR_QT.pro CONFIG+=release; make)" >&2
    exit 1
fi

# Names as in the .deb: a pre-release (not a release candidate) is the "-test" variant.
appli_name="OSCAR20"
if [[ "$VERSION" == *-* && "$VERSION" != *rc* ]]; then
    appli_name="OSCAR20-test"
fi
icon_name="$appli_name"

fetch() {   # name url sha256
    local file="$tools/$1"
    if [ ! -x "$file" ]; then
        echo "Downloading $1"
        mkdir -p "$tools"
        curl -sSfL -o "$file.part" "$2"
        mv "$file.part" "$file"
    fi
    if ! echo "$3  $file" | sha256sum -c --quiet -; then
        echo "Checksum mismatch for $file" >&2
        exit 1
    fi
    chmod +x "$file"
}
fetch linuxdeploy-x86_64.AppImage \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_VERSION/linuxdeploy-x86_64.AppImage" \
    "$LINUXDEPLOY_SHA256"
fetch linuxdeploy-plugin-qt-x86_64.AppImage \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/$QTPLUGIN_VERSION/linuxdeploy-plugin-qt-x86_64.AppImage" \
    "$QTPLUGIN_SHA256"
fetch appimagetool-x86_64.AppImage \
    "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_VERSION/appimagetool-x86_64.AppImage" \
    "$APPIMAGETOOL_SHA256"

work=$(mktemp -d "${TMPDIR:-/tmp}/oscar-appimage.XXXXXX")
trap 'rm -rf "$work"' EXIT
appdir="$work/AppDir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share/applications" \
         "$appdir/usr/share/icons/hicolor/48x48/apps" "$appdir/usr/share/icons/hicolor/scalable/apps"

# OSCAR looks for Html, Translations and Help next to its executable first.
cp "$binary" "$appdir/usr/bin/$appli_name"
for d in Html Translations Help; do
    [ -d "$build_folder/oscar/$d" ] && cp -r "$build_folder/oscar/$d" "$appdir/usr/bin/"
done
cp "$here/$icon_name.png" "$appdir/usr/share/icons/hicolor/48x48/apps/$icon_name.png"
cp "$here/$icon_name.svg" "$appdir/usr/share/icons/hicolor/scalable/apps/$icon_name.svg"
# The .deb's desktop file, with the icon by name and no TryExec (the AppImage isn't on PATH).
sed -e "s|^Icon=.*|Icon=$icon_name|" -e '/^TryExec=/d' -e "s|^Exec=.*|Exec=$appli_name|" \
    "$here/$icon_name.desktop" > "$appdir/usr/share/applications/$appli_name.desktop"

export QMAKE=${QMAKE:-$(command -v qmake6 || command -v qmake)}
export ARCH=x86_64
# Wayland as well as X11, where the Qt build has it.
platforms=$("$QMAKE" -query QT_INSTALL_PLUGINS)/platforms
extra=""
for p in libqwayland-generic.so libqwayland-egl.so; do
    [ -f "$platforms/$p" ] && extra="${extra:+$extra;}$p"
done
export EXTRA_PLATFORM_PLUGINS="$extra"
export PATH="$tools:$PATH"   # linuxdeploy finds its Qt plugin on PATH

linuxdeploy-x86_64.AppImage --appdir "$appdir" \
    --executable "$appdir/usr/bin/$appli_name" \
    --desktop-file "$appdir/usr/share/applications/$appli_name.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/48x48/apps/$icon_name.png" \
    --plugin qt

out="$here/$appli_name-$VERSION-x86_64.AppImage"
rm -f "$out"
appimagetool-x86_64.AppImage --no-appstream "$appdir" "$out"
echo "AppImage: $out"
