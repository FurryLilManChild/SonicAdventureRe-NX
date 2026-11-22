SonicAdventureRe-nx
===================

A native Nintendo Switch port of Sonic Adventure (Dreamcast), built on reshaped's
sonic-adventure decompilation (https://github.com/reshaped/sonic-adventure).
Unofficial fan project. Sonic Adventure belongs to SEGA. No game data is included:
you need your own discs.

What you need
-------------
- A Switch running Atmosphere custom firmware.
- Your own dump of the Japanese Sonic Adventure disc (HDR-0001, version 1.007), as a .gdi with
  its track files or a Redump .cue with its .bin files.
- Optional, for English voices and text: your own dump of the US Sonic Adventure disc, the same
  way.

Install
-------
1. Copy the "switch" folder from this zip to the root of your SD card.
2. Copy your Japanese disc image (the .gdi or .cue and all its track files) into
   sdmc:/switch/SonicAdventureRe-nx/disc/
3. Optional: copy your US disc image the same way into
   sdmc:/switch/SonicAdventureRe-nx/us_disc/
4. Launch through title takeover: hold R while opening any game, then pick
   SonicAdventureRe-nx in the Homebrew Menu. The Album does not give it enough memory.

First launch
------------
The first launch unpacks your disc images (about 1 GB each, so leave about 5 GB free on the
SD card), builds the game from the Japanese disc and saves it as
SonicAdventureRe-nx.built.img, and makes the english folder from the US disc.
This takes a few minutes. Later launches start straight away. Once it has worked, you can
delete the .gdi/.cue and track files from disc/ and us_disc/ to save space (keep the
unpacked files and extracted.txt).

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
