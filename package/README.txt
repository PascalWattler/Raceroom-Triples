RaceRoom Triples
================

Triple-screen resolutions for RaceRoom on three separate monitors, without
NVIDIA Surround or AMD Eyefinity. Unofficial community tool by Pascal Wattler.

Project page, source code and updates:
https://github.com/PascalWattler/Raceroom-Triples


USE AT YOUR OWN RISK
--------------------
This tool has no approval from KW Studios, the developer of RaceRoom. I asked
them about it and have not received an answer. I use it myself, including
online, and have had no problems so far. There is no guarantee, and I accept
no liability for bans, lost progress or any other consequence of using it.


WHAT IS IN THIS ZIP
-------------------
d3d9.dll             the wrapper
RaceRoomTriple.ini   the list of resolutions it adds (you can edit it)
README.txt           this file
LICENSE.txt          license of this tool (MIT)
licenses\            licenses of the compiler runtime contained in the DLL


WHAT YOU NEED
-------------
- The 64-bit version of RaceRoom on Steam. Direct3D 9 is the default; for
  DXVK see the section DXVK below.
- Three monitors that Windows uses as an extended desktop.
- RaceRoom set to Borderless Window. Exclusive fullscreen does not work.
- No other d3d9.dll in the game folder (for example ReShade, or a DXVK you
  copied there yourself).


INSTALL
-------
1. BEFORE you copy anything: start RaceRoom, set the display mode to
   Borderless Window, apply, and exit the game completely.

2. In Steam, right-click RaceRoom, choose Manage > Browse local files and
   open the folder Game\x64. This is the folder that contains RRRE64.exe.
   The usual path is:

   C:\Program Files (x86)\Steam\steamapps\common\raceroom racing experience\Game\x64

   "Game" and "Game\x64" are different folders. It must be Game\x64.

3. If there is already a d3d9.dll in that folder, find out what it belongs
   to first. Do not overwrite a DLL from another tool.

4. Copy d3d9.dll and RaceRoomTriple.ini from this ZIP into that folder.
   Do not copy anything into the Windows folder.

5. Start RaceRoom. In the video settings, select the aspect ratio 48:9 and
   the resolution that matches your three screens together:

   3 x 1920x1080   ->   5760 x 1080
   3 x 2560x1440   ->   7680 x 1440
   3 x 3840x2160   ->  11520 x 2160

6. Enable RaceRoom's triple-screen option and enter your screen sizes,
   angles and distance in the game.

The refresh rates in the list are menu entries only. Windows and your
graphics driver still decide the real refresh rate of each monitor.


IF SOMETHING GOES WRONG
-----------------------
The game shows a D3D9 "unsupported" error at startup:
   RaceRoom was last saved in exclusive fullscreen. Close the game, move
   d3d9.dll out of Game\x64, start the game, switch to Borderless Window
   and exit. Then put the DLL back and start again.

The new resolutions do not appear:
   Check that the DLL is in Game\x64 next to RRRE64.exe, and that you
   restarted the game after copying it. Start the game the normal way, not
   through Steam's DXVK launch option (see DXVK below).

AMD Radeon RX 5000 or newer, the game freezes, shows a black screen or
crashes:
   On Direct3D 9, RaceRoom can do this on these cards when MSAA and motion
   blur are both enabled, no matter if the wrapper is being used or not. 
   Disable motion blur (or MSAA) in the video settings before you change
   the resolution, or switch to DXVK (see DXVK below). This comes from
   reports by other users; I have no AMD card to test and confirm it.

Windows or your virus scanner warns about the DLL:
   The DLL is not digitally signed. The release page on GitHub lists the
   SHA256 checksum of this ZIP, so you can check your download.

The game does not start at all or crashes:
   Remove d3d9.dll from Game\x64. The game then runs as before.
   Open an issue on github and provide the logfile of the run to help me
   fixing a potential bug. 

Log file:
   The wrapper writes a log for every game start to

   %LOCALAPPDATA%\RaceRoomTripleHelper

   (paste that line into the address bar of Windows Explorer). Nothing is
   sent anywhere. You can delete old logs at any time. If you report a
   problem on GitHub, attach the log. It contains the path of your game
   folder, so remove anything you prefer to keep private.


UNINSTALL
---------
Close RaceRoom and delete d3d9.dll and RaceRoomTriple.ini from Game\x64.
If the game has saved a triple resolution, select a normal resolution in
the game before you remove the DLL.


OTHER RESOLUTIONS
-----------------
Open RaceRoomTriple.ini in a text editor. Each line under [Modes] is

   total width x height = refresh rates

for example  7680x1440 = 60,120,144.  For sizes that are not exactly 48:9,
such as a bezel-corrected width, set StrictTripleAspect = false. Save the
file as plain text and restart the game. All details are on the project
page.


DXVK
----
RaceRoom ships DXVK (Direct3D 9 on Vulkan) in the folder Game\x64dxvk.
Steam's DXVK launch option starts the game from that folder, where the
wrapper is not loaded. To use the triple resolutions with DXVK:

1. Install the wrapper in Game\x64 as described above.
2. In RaceRoomTriple.ini, set  Renderer = dxvk  under [Settings].
3. Start RaceRoom the normal way, NOT via Steam's DXVK launch option.

The wrapper then uses the DXVK from Game\x64dxvk. It changes nothing in
that folder. Its log shows  RENDERER active=dxvk  when DXVK is in use. If
DXVK cannot be loaded, the game starts on Direct3D 9 and the log says so.

Tested at 7680 x 1440 with an RTX 5080. Not tested on AMD or Intel cards.


SUPPORT
-------
If this tool is useful to you, you can buy me a coffee:
https://buymeacoffee.com/waddel
