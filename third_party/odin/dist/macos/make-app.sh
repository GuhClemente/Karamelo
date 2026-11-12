#!/bin/sh
# Make Odin.app from an install, with MoltenVK from $VULKAN_SDK or $MOLTENVK
#
#   dist/macos/make-app.sh <install dir> <Odin.app> [<version>]
set -eu
prefix=${1:?usage: make-app.sh <install dir> <Odin.app> [<version>]}
app=${2:?usage: make-app.sh <install dir> <Odin.app> [<version>]}
version=${3:-0}
dist=$(cd "$(dirname "$0")/.." && pwd)

moltenvk=${MOLTENVK:-}
if [ -z "$moltenvk" ]; then
	for candidate in "${VULKAN_SDK:-}/lib/libMoltenVK.dylib" "${VULKAN_SDK:-}/macOS/lib/libMoltenVK.dylib" \
			/usr/local/lib/libMoltenVK.dylib; do
		if [ -f "$candidate" ]; then
			moltenvk=$candidate
			break
		fi
	done
fi
if [ -z "$moltenvk" ]; then
	echo "make-app.sh: no libMoltenVK.dylib (VULKAN_SDK or MOLTENVK)" >&2
	exit 1
fi

rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources" "$app/Contents/Frameworks" "$app/Contents/share"
cp "$prefix/bin/odin" "$app/Contents/MacOS/odin"
cp -R "$prefix/share/odin" "$app/Contents/share/odin"
cp "$moltenvk" "$app/Contents/Frameworks/libMoltenVK.dylib"
sed "s/@VERSION@/$version/g" "$dist/macos/Info.plist" > "$app/Contents/Info.plist"

iconset=$(mktemp -d)/odin.iconset
mkdir -p "$iconset"
for size in 16 32 128 256 512; do
	sips -z $size $size "$dist/odin.png" --out "$iconset/icon_${size}x${size}.png" >/dev/null
	sips -z $((size * 2)) $((size * 2)) "$dist/odin.png" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$app/Contents/Resources/odin.icns"
rm -rf "$(dirname "$iconset")"

codesign --force --deep --sign - "$app"
echo "make-app.sh: $app, with $moltenvk"
