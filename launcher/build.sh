#!/usr/bin/env bash
# Build an XEX2 module with the Xbox 360 SDK under wine.
#   launcher/build.sh                          -> launcher/build/launcher.xex       (the launcher, a title executable)
#   launcher/build.sh tests/xenia_compat [dll] -> launcher/build/xenia_compat.xex   (a test module; dll is the default)
#   launcher/build.sh tests/xextest exe        -> launcher/build/xextest.xex
# The launcher is every .c/.cpp under src/ (each folder on the include path), third_party/microui and common/md5.c.
# Needs XEDK (default ~/opt/xedk-21256.3/xedk) and a wine prefix (default ~/.local/share/wineprefixes/xedk).
# The SDK is never copied into this repository.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
out="$here/build"
XEDK=${XEDK:-$HOME/opt/xedk-21256.3/xedk}
export WINEPREFIX=${WINEPREFIX:-$HOME/.local/share/wineprefixes/xedk} WINEDEBUG=${WINEDEBUG:--all}
[[ -f "$XEDK/bin/win32/cl.exe" ]] || { echo "XEDK not found at $XEDK" >&2; exit 1; }

if [[ $# -eq 0 ]]; then
  name=launcher kind=exe xexml="$here/xex.xml"
  mapfile -t dirs < <(find "$here/src" -type d | sort)
  dirs+=("$here/third_party/microui" "$root/common")
  mapfile -t sources < <(find "$here/src" -type f \( -name '*.c' -o -name '*.cpp' \) | sort)
  sources+=("$here/third_party/microui/microui.c" "$root/common/md5.c")
else
  src="$here/${1%/}"
  [[ -d "$src" ]] || { echo "no module directory $src" >&2; exit 1; }
  name=$(basename "$src") kind=${2:-dll} xexml="$src/xex.xml"
  dirs=("$src")
  mapfile -t sources < <(find "$src" -maxdepth 1 -type f \( -name '*.c' -o -name '*.cpp' \) | sort)
fi

# Wine leaves background processes (wineserver, services) holding whatever stdout the first wine call had.
# Run every wine command with output in a file, so a caller reading this script's stdout through a pipe
# is not blocked until those processes exit.
log=$(mktemp)
trap 'rm -f "$log"' EXIT
w() {
  local rc=0
  wine "$@" >"$log" 2>&1 </dev/null || rc=$?
  cat "$log"
  return "$rc"
}
win() { printf 'Z:%s' "${1//\//\\}"; }

bin="$XEDK/bin/win32"
export INCLUDE="$(win "$XEDK/include/xbox")" LIB="$(win "$XEDK/lib/xbox")"
includes=()
for d in "${dirs[@]}"; do
  includes+=("/I$(win "$d")")
done

mkdir -p "$out/$name"
cd "$out/$name"
objs=()
for c in "${sources[@]}"; do
  o="$(basename "${c%.*}").obj"
  w "$bin/cl.exe" /nologo /c /O2 /W3 /GS- /D_XBOX /DNDEBUG "${includes[@]}" /Fo"$o" "$(win "$c")"
  objs+=("$o")
done
xexconfig=()
[[ -f "$xexml" ]] && xexconfig=("/XEXCONFIG:$(win "$xexml")")
case "$kind" in
  dll) w "$bin/link.exe" /nologo /DLL /OUT:"$name.dll" /SUBSYSTEM:XBOX /MACHINE:PPCBE /ENTRY:_DllMainCRTStartup \
         "${xexconfig[@]}" "${objs[@]}" xapilib.lib xboxkrnl.lib libcmt.lib
       built="$name.dll" ;;
  exe) w "$bin/link.exe" /nologo /OUT:"$name.exe" /SUBSYSTEM:XBOX /MACHINE:PPCBE \
         "${xexconfig[@]}" "${objs[@]}" d3d9.lib d3dx9.lib xgraphics.lib xnet.lib xapilib.lib xboxkrnl.lib libcmt.lib
       built="$name.exe" ;;
  *) echo "kind must be dll or exe" >&2; exit 1 ;;
esac
# link.exe runs the XEX builder itself: the .dll/.exe it writes is already XEX2 (.pe is the intermediate PE).
# Shipped as built, encrypted with the development key: an RGH console on 2.0.17559 starts that form, and refused
# the payload-decrypted forms of tools/xex-retail.py (console test 1, 2026-11-03).
cp "$built" "$out/$name.xex"
ls -l "$out/$name.xex"
