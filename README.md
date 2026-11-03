# 114360

Launcher and installer packages for running the final Xbox 360 FINAL FANTASY XI client (`40160203_0`) against
private servers.

## Packages (`packages/`)

| File | For |
|---|---|
| `114360-installer-rev4-windows.zip` | Windows |
| `114360-installer-rev4-linux.zip` | Linux |

Each zip holds the installer (`114360-installer`), its libraries, the launcher build (`payload/default.xex`), the
font and a guide (`README.txt`). No game data is included; the installer fetches PlayOnline and FFXI data itself.
`SHA256SUMS` lists the checksums.

## rev4

- Advanced option "For Xbox 360 Neighborhood" (-neighborhood): leaves out the three empty files and gives the
  four PlayOnline menu tracks with names over 42 characters shorter names, so the install copies to an Xbox 360
  hard disk without refusals. Off: every file is copied as it is. The game's own error logs are never copied.

- The 2016 data folder no longer needs CONFIG.SYS: when it has none, the installer writes the FFXI disc's own.
- An interrupted install continues when the installer is run again with the same choices.

Earlier packages are on the Releases page.

## Launcher build of 2026-11-02

- Bag sizes: today's servers send packet 0x01C in a new layout; the launcher puts it back into the 2016 layout.
- Bags the 2016 client does not have (wardrobes 2-8, recycle bin): their item packets are dropped instead of
  overwriting client memory.
- PlayOnline friend list on the chat page: `/friends`, `/friend add|accept|remove ID`; accounts without a
  PlayOnline handle get one at sign-in.
- Project Crystal (PS2 tag): the 2010 bag size conversion now writes the 2016 layout.
