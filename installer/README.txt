114360 - EN - Install Guide for XBOX 360 / Xenia - rev4

Hello, this is the installation guide for FFXI for 360. Some things need to be covered first:

1. The launcher saves the passwords of your PlayOnline accounts in launcher.ini in PLAIN TEXT.
2. The launcher was built with a later XDK than the NXE and Blades dashboards; expect unexpected crashes.
3. The launcher hasn't been tested on real hardware. It should work, but emulation and hardware differ.
4. If the folder structure doesn't match what follows, expect a crash. The in-game menu MIGHT crash outright
   on a console where Xenia doesn't: exceptions thrown there mean something is off.


WHAT YOU NEED

  - Your FINAL FANTASY XI 2016 client (40160203_0): a folder holding 0001\ (FFXi.dll, FFXiMain.dll,
    ROM\, ROM2\ ...) or those files directly. Required. CONFIG.SYS is written when it is missing.
  - Optionally the September 2010 data, same layout. Program files it lacks are taken from the 2016 client.
  - A FINAL FANTASY XI Xbox 360 disc image (.iso), or a copy of its PlayOnline folder. Only the PlayOnline
    data is taken from it; none of its programs run.

  PolCore (PlayOnline 20110726_A) and the PlayOnline files changed after the disc are downloaded from
  PlayOnline's update server, which still answers. You can give your own PolCore.xex instead. area.dat is
  4 bytes (the region code); one is written if you have none.


INSTALL

  Windows: 114360-installer.exe        Linux: ./114360-installer

  "Install type" at the top of the window picks the kind of install:
    Simple install    two tabs: Info (paste the Secret Key, Check key, Continue) and Install (the questions:
                      how you bring it to the Xbox, where, then Install; the live log shows there once it runs).
                      See SIMPLE INSTALL.
    Regular install   six tabs, from your own disc and patch sets:
    1. Info          what you need.
    2. Game files    the work location (where files are processed; empty = 114360-work next to the
                     installer), the 2016 patch set (XB2-40160203_0) and the disc image or its PlayOnline folder,
                     and the 2010 PS2 patch set (20100904_2). Each has Browse and Check.
    3. PlayOnline    Install from SE (PolCore 20110726_A from PlayOnline's update server, PolCore 20100601_1 for
                     Project Crystal, and the PlayOnline files changed after the disc) or Install from Local
                     (your own PolCore.xex and area.dat, with Check).
    4. Advanced      Direct Install (plain folders instead of shared data), including the Xbox PlayOnline files
                     the console cannot hold (empty ones, names over 42 characters: both break real hardware),
                     2010 data sources, and Generate Secret Key.
    5. Install       the install method, where to install, Check, Install, and the progress.
    6. Log           the installer's latest log lines as they happen (the same as file-114360-installer.log).
  Installing again later only copies what changed, and an install that was stopped continues where it stopped.

  Shared data (the default) stores each data file once for all clients. Check says which storage you get and
  what it saves, e.g. "storage: POOLED, game data about 11.7 GB instead of 21.8 GB as plain folders (saves
  10.2 GB, 47%)" for both clients, and the final "Done" line repeats it.

  Install methods (Install tab):
    USB drive                      a FAT32/exFAT USB drive the console reads. Files as they are.
    FTP                            straight to the console: its address (Aurora's or DashLaunch's FTP server),
                                   user and password (xboxftp/xboxftp by default) and a folder such as
                                   /Hdd1/114360. Nothing is staged on the PC; files already there at the same size
                                   are skipped, and a dropped connection is reopened.
    Xbox 360 Neighborhood          browse DIRECTLY to the console in Neighborhood (or write a folder on the PC
                                   that you then copy over with it).
    This computer first (Xenia)    a folder on the PC: run it in Xenia, or copy it to the console later.
  The console's file system (FATX) takes names of 42 characters at most and Neighborhood refuses empty files, so
  every target leaves out the files the console cannot hold (unless Advanced says otherwise): three empty files and
  four PlayOnline Viewer menu music tracks with longer names, e.g. "Gin_no_kaichudokei_(by_Noriko_Matsueda).104.scd".
  Only the Viewer plays them, and the launcher starts FFXI without it.

  Command line, without the window:
    114360-installer <INSTALL LOCATION> -4016 <2016 root> [-2010 <2010 root>] -iso <disc.iso>
                     -polcore download [-polcore2010] -update-pol [-neighborhood] [-no-share] [-check]
    114360-installer <INSTALL LOCATION> -key <secret key, or @file holding it> [-work <folder>] [-neighborhood]
  The first is a Regular Install, the second a Simple Install (the packs are downloaded into -work, by default
  114360-work next to the installer; packs already there are checked and reused).
  <INSTALL LOCATION> is a folder, or ftp://[user[:password]@]console[:port]/Hdd1/114360 to install over FTP.
  -neighborhood is the Neighborhood target.


SIMPLE INSTALL (SECRET KEYS)

  Someone who already has the game files can host a ready-made install on Google Drive and give you a Secret Key
  for it: a block of text. Choose Simple install at the top, paste it on Info, press Check key and Continue; the
  Install tab then asks what you use to bring it to the Xbox, where to put it, checks the space, and installs. You
  need an internet connection and the space the key's install reports ("Data Required to Download"). Every
  download is checked (SHA-512 per pack, MD5 per file); a stopped download continues where it stopped.

  Keys made by the 114360 project are signed; the installer says SIGNED for them and refuses a key whose signature
  does not match (ZZ102). Keys made by anyone else install with an UNSIGNED warning: only use Secret Keys from
  people you trust. An existing launcher.ini is kept; the presets it lacks are added.

  To make a key (Regular install, Advanced tab, Generate Secret Key): pick the installs (2016, 2010, ...; pooled
  or direct), and the installer builds the hosted folder (manifest.xml.zlib and pack-NNN.bin) in the work
  location. Upload that folder to Google Drive, share it as "Anyone with the link", and paste the folder's and the
  manifest's Drive IDs to get the key. The format is in docs/installer-simple.md.


ERRORS, REPORTS AND THE LOG

  Every problem has a code, ZZ and three digits; the text next to it says what to do.
    ZZ1xx  the Secret Key or its manifest      ZZ101 not a key, ZZ102 bad signature, ZZ103-106 manifest
    ZZ2xx  downloading                         ZZ201 no connection, ZZ202 Drive refused, ZZ203 pack damaged,
                                               ZZ204 pack missing from the folder
    ZZ3xx  where you install                   ZZ301 not enough space, ZZ302 write failed, ZZ303 FTP login,
                                               ZZ304 FTP connection lost, ZZ305 target not found
    ZZ4xx  your game files                     ZZ401 2016 client not found, ZZ402 wrong 2016 build, ZZ403 2010
                                               data, ZZ404 disc image, ZZ405 PolCore
    ZZ5xx  checking files                      ZZ501 file differs, ZZ502 decompression failed
    ZZ9xx  the installer itself                ZZ901 out of memory, ZZ902 cancelled, ZZ999 unexpected
  Copy report (under Progress, on every tab) puts the progress on the clipboard as text, plus an
  "encoded: 114360R1:..." line that holds the same thing for pasting where text gets mangled. Everything the
  installer does is written to file-114360-installer.log next to it (or the user's settings folder when that is
  read-only): send it along when asking for help.

THE RESULT

  <folder>\default.xex, launcher.ini     the launcher
  <folder>\area.dat, PlayOnline\         PlayOnline's data
  <folder>\rev\XB2-40160203_0\           the 2016 client (required)
  <folder>\rev\PS2-20100904_2\           the 2016 client with 2010 (PS2) data (optional)
  <folder>\data\                         the game data, each file stored once ("Share data between versions")
  <folder>\pol\X2U-20100601_1\           the 2010 PolCore (optional)

  Revisions are named after their data: XB2 = Xbox 360, W20 = PC, PS2 = PS2, then the data's version.
  With shared data both clients together take about 12 GB; with -no-share (plain folders) about 11 GB each.
  Installing again with the other setting switches an existing install over and removes the old mode's files.
  An install made by an older installer (rev\40160203_0, rev\sep2010) is renamed, presets included.


RUNNING IT

  Aurora: put <folder> on a USB drive or the HDD (Hdd1:\Games\FFXI, for example), then
  Settings -> Content -> Scan Paths, depth 1, and rescan. You can also open default.xex in the File Manager.
  It shows up as FINAL FANTASY XI.

  Xenia: install to any folder on the PC and open <folder>\default.xex (File -> Open, or
  xenia_canary <folder>\default.xex). Xenia mounts that folder as the game's drive, so the launcher, both
  clients and the shared data run from it as they are; nothing is copied anywhere else. For a server on the
  same PC use 127.0.0.1. Start Xenia with keyboard_mode = 4 (the 114360 Xenia build: every key goes to the game,
  Xenia's own hotkeys need Ctrl+Shift; on stock Xenia Canary use 2) to type in the menus and the chat page. Under
  stock Xenia Canary, leaving the game shows Xenia's "restart Xenia" message: start it again.

  No DNS or other network tomfoolery is needed: servers are given by IPv4 address, so any server can be
  reached. launcher.ini comes with no servers: add yours in the menu (New server, then its address), with your
  PlayOnline ID and password. A USB keyboard works for the menus and the chat page.


CREDITS AND LICENSES

  114360-installer -credits prints who made what and every license in full (on Windows, run it from a command
  prompt). In short: the launcher contains code reworked from Project Crystal Server (GNU AGPL 3.0), microui
  (MIT) and the X.Org misc-fixed fonts (public domain); the installer uses DragonGuilt PreSDL (BSD 3-Clause),
  SDL3 (zlib), FreeType, zlib, libpng and bzip2 on Windows, Monocypher (BSD 2-Clause or CC0) and Noto Sans
  (SIL OFL 1.1). libcurl and Fontconfig on Linux and WinHTTP on Windows come from the system.
