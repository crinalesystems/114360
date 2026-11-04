#!/usr/bin/env bash
# Build the native installer for Linux and Windows
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
# DragonGuilt PreSDL (the window toolkit): the copy in third_party/presdl/ (from CrinaleSystemsEngine16/abets);
# PRESDL=<dir> builds against another one.
PRESDL=${PRESDL:-$HERE/third_party/presdl}
FONT=${FONT:-/usr/share/fonts/google-noto/NotoSans-Regular.ttf}
OUT=$ROOT/work/installer-native
MONO=$HERE/third_party/monocypher
SRC=("$HERE/src/installer.c" "$HERE/src/polp.c" "$HERE/src/ftp.c" "$ROOT/common/md5.c" "$HERE/src/net.c"
     "$HERE/src/simple.c" "$HERE/src/pack.c" "$MONO/monocypher.c" "$MONO/monocypher-ed25519.c")
FLAGS=(-std=c23 -O2 -Wall -Wno-unused-function -Wno-format-truncation -I"$PRESDL" -I"$HERE/src"
       -I"$HERE/third_party" -I"$ROOT/common")

for os in linux windows; do
    rm -rf "${OUT:?}/$os"
    mkdir -p "$OUT/$os/payload"
    cp "$FONT" "$OUT/$os/payload/font.ttf"
done

gcc "${FLAGS[@]}" -o "$OUT/linux/114360-installer" "${SRC[@]}" \
    $(pkg-config --cflags --libs sdl3 freetype2 fontconfig libcurl) -lz -Wl,-rpath,'$ORIGIN' -s
cp -L /usr/lib64/libSDL3.so.0 "$OUT/linux/"

MINGW=/usr/x86_64-w64-mingw32/sys-root/mingw
x86_64-w64-mingw32-gcc "${FLAGS[@]}" -I"$HERE/third_party/nofontconfig" -I"$MINGW/include" -I"$MINGW/include/freetype2" \
    -o "$OUT/windows/114360-installer.exe" "${SRC[@]}" -L"$MINGW/lib" -lSDL3 -lfreetype -lws2_32 -lwinhttp -lz \
    -mwindows -s -static-libgcc
for dll in SDL3.dll libfreetype-6.dll zlib1.dll libpng16-16.dll libbz2-1.dll; do
    cp "$MINGW/bin/$dll" "$OUT/windows/"
done
du -sh "$OUT/linux" "$OUT/windows"

if [[ "${1:-}" == "simple-test" ]]; then
    # Its own folder: package-installer.sh zips the whole linux/ folder, and the test is not for players.
    echo "Building simple-test..."
    mkdir -p "$OUT/tests"
    gcc "${FLAGS[@]}" -o "$OUT/tests/simple-test" "$HERE/tests/simple-test.c" "$HERE/src/net.c" "$HERE/src/simple.c" \
        "$MONO/monocypher.c" "$MONO/monocypher-ed25519.c" "$ROOT/common/md5.c" \
        $(pkg-config --cflags --libs libcurl) -lz -s
    echo "simple-test built: $OUT/tests/simple-test"
fi
