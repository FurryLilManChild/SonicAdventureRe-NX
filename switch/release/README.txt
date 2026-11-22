SonicAdventureRe-nx
===================

A native Nintendo Switch port of Sonic Adventure (Dreamcast), built on reshaped's
sonic-adventure decompilation (https://github.com/reshaped/sonic-adventure).
Unofficial fan project. Sonic Adventure belongs to SEGA. No game data is included:
you need your own discs.

What you need
-------------
- A Switch running Atmosphere custom firmware.
- Your own Japanese Sonic Adventure disc's files (HDR-0001, version 1.007).
- Optional, for English voices and text: your own US Sonic Adventure disc's files.

Install
-------
1. Copy the "switch" folder from this zip to the root of your SD card.
2. Copy your Japanese disc's files into sdmc:/switch/SonicAdventureRe-nx/disc/
   (1ST_READ.BIN, and the SONICADV folder with everything in it).
3. Optional: copy your US disc's files into sdmc:/switch/SonicAdventureRe-nx/us_disc/
   (its SONICADV folder with everything in it).
4. Launch through title takeover: hold R while opening any game, then pick
   SonicAdventureRe-nx in the Homebrew Menu. The Album does not give it enough memory.

First launch
------------
The first launch builds the game from your disc files and saves it as
SonicAdventureRe-nx.built.img, then makes the english folder if us_disc has files.
This takes a few minutes. Later launches start straight away.

If a disc file is missing or is not the expected version, the screen says which one.

Settings
--------
Press + and - together in game for the port menu: resolution, voices (Japanese or
English), text (Japanese or English), and more. Settings are saved in settings.txt.

Reporting problems
------------------
Send SonicAdventureRe-nx.log from sdmc:/switch/SonicAdventureRe-nx/, and for a crash,
the report from sdmc:/atmosphere/crash_reports/ (its file name starts with a number;
give that number).
