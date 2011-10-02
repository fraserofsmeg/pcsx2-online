PCSX2-online
============

PCSX2-online is a fork of open-source PlayStation 2 emulator PCSX2 with netplay. Now you can play PS2 games with your friends on the internet!

How to start netplay
--------------------

System -> Boot Netplay. The connection dialog is pretty self-explanatory.

What you should know
--------------------

* Each side must use the same bios file
* Each side must use the same game image
* Make sure you have the same memory cards before playing, or memory cards will be disabled after consistency check - all the settings will reset to defaults, no unlockables, and you won't be able to save during the game. You can disregard that if memory cards are not really needed.

Features
--------

* Game and bios image checks between sides
* Automatic input delay detection
* Memory cards are being disabled if they are not consistent on each side.

Notes
-----

During netplay, settings are changed to defaults with all speedhacks disabled, so performance may drop a bit. 'Skip Mpeg' hack is enabled during netplay.

Tested and working
------------------

* Melty Blood Actress Again (Netplay was basically was made for this game alone)
* Guilty Gear XX Accent Core (US Release)

Tested and not working (i.e. will be fixed soon)
------------------------------------------------

* Tekken 5