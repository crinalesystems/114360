# The 114360 launcher

`default.xex` for the Xbox 360 (and Xenia): it starts FINAL FANTASY XI's 2016 client (`40160203_0`) against
today's private servers. It signs in to PlayOnline itself, picks the data revision and server from
`launcher.ini`, and converts packets between server and client layouts. Licensed under the GNU AGPL 3.0
(`LICENSE`): its PlayOnline code is reworked from Project Crystal Server.

| Folder | Contents |
|---|---|
| `src/` | `launcher.c` (start-up, config, hooks, packet conversion, install check), `patches.c`, `sha1.c`, `ssl3.c` |
| `src/pol/` | PlayOnline: sign-in, profile, chat and registration (`polcrypt`, `polirc`, `polnet`, `polreg`) |
| `src/ui/` | the menu (`menu.cpp`) and its bitmap font (`font.h`, from `tools/gen-launcher-font.py`) |
| `src/engine/` | per-build facts of the client (`engine.c`), import thunks (`engine_imports.h`, from `tools/gen-engine-imports.py`) |
| `third_party/microui/` | microui 2.02 (MIT) |
| `tests/` | test modules: `xextest`, `pkgtest`, `xenia_compat` |
| `../common/md5.c` | MD5, shared with the installer |

## Building

Needs the Xbox 360 SDK (XDK 2.0.21256.3), which is not included, and wine:

    launcher/build.sh                          # -> launcher/build/launcher.xex
    launcher/build.sh tests/xenia_compat       # a test module (dll; add exe for a title)

`XEDK` and `WINEPREFIX` point at the SDK and the wine prefix (defaults `~/opt/xedk-21256.3/xedk`,
`~/.local/share/wineprefixes/xedk`). Installing it on a console or in Xenia: the installer's `README.txt`.
