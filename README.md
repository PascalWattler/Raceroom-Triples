# RaceRoom Triples without NVIDIA Surround

I built this 64-bit Direct3D 9 wrapper to use RaceRoom’s built-in triple-screen projection across three independent monitors in windowed mode. I wanted to keep my five-monitor Windows desktop layout without enabling NVIDIA Surround. RaceRoom handles the triple projection and FOV. I use the DLL on its own, without a separate window-resizing tool.

## Install and run

1. **Before installing the DLL, start RaceRoom and switch to Windowed mode.** Apply the setting and exit the game completely.
2. In Steam, open RaceRoom’s **Manage → Browse local files** and navigate to `Game\x64`. This is the folder containing `RRRE64.exe`.
3. Get the 64-bit `d3d9.dll` from [Releases](https://github.com/PascalWattler/Raceroom-Triples/releases) if a packaged build is available, or [build it from source](#build-from-source). Copy it into that folder. If there is already a `d3d9.dll` there, identify it first. I don’t support combining this wrapper with another local D3D9 proxy, such as DXVK or ReShade. Add `RaceRoomTriple.ini` alongside the DLL if you want custom modes.
4. Start RaceRoom. In the video settings, select the **48:9** aspect ratio and the combined resolution that matches your screens. Enable RaceRoom’s triple-screen projection and set the screen geometry and FOV in the game.

| Setup | Combined resolution | My test status |
| --- | --- | --- |
| 3 × 1920 × 1080 | 5760 × 1080 | Backbuffer creation verified on my second system |
| 3 × 2560 × 1440 | 7680 × 1440 | Triple projection tested on my rig. Backbuffer creation also verified on my second system |
| 3 × 3840 × 2160 | 11520 × 2160 | Offered by the wrapper. I haven’t verified backbuffer creation or rendering at this size |

**If you see a D3D9 “unsupported” error at startup**, check whether RaceRoom was last saved in exclusive fullscreen. I reproduced this on my second PC. The log showed `Windowed=0` followed by `BLOCKED: exclusive fullscreen`. If RaceRoom won’t reach the menu, close it, temporarily move my DLL out of `Game\x64`, start the game, apply Windowed mode and exit. Put the DLL back and restart.

**To uninstall**, close RaceRoom and remove the copy of `d3d9.dll` you installed, along with `RaceRoomTriple.ini` if you added it. If the game has saved the triple resolution, select a standard resolution in the game before removing the DLL.

## Configure resolutions and refresh entries

Version 0.4 reads an optional `RaceRoomTriple.ini` from the DLL folder at startup. To use this example, save it as plain text with that exact filename and restart RaceRoom after making changes.

```ini
[Settings]
StrictTripleAspect = true

[Modes]
7680x1440 = 60,75,100,120,144
5760x1080 = 60,75,100,120,144
11520x2160 = 60,75,100,120,144
```

With `StrictTripleAspect = true`, the combined dimensions must match 48:9 exactly. Set it to `false` for custom ratios between 1:1 and 12:1, such as a bezel-corrected width. Each row uses `WIDTHxHEIGHT = Hz,Hz,...`. The frequencies are values for RaceRoom’s mode list. Windows and the graphics driver still control each monitor’s actual refresh rate.

If the INI is missing, unreadable or contains no valid modes, the wrapper offers the three resolutions above at 60 and 144 Hz. It ignores duplicate entries, logs invalid rows and keeps valid ones. The parser accepts refresh entries from 24 to 1000 Hz and up to 256 distinct modes. The wrapper uses the adapter’s reported texture size limits where available. RaceRoom and the driver still have to create the render surfaces successfully.

## What I changed in Direct3D 9

My first attempts to resize the window stretched the existing image. RaceRoom also reverted a manually configured 7680 × 1440 resolution to 1024 × 768. That behavior led me to try adding the requested dimensions to the adapter mode list that RaceRoom reads.

I load the real Windows `d3d9.dll` by its absolute system path and forward calls through COM wrappers for `IDirect3D9/Ex` and `IDirect3DDevice9/Ex`. I append the configured entries to the mode enumeration returned by `GetAdapterModeCount`, `EnumAdapterModes` and their Ex variants. RaceRoom then selects a mode and asks Direct3D to create or resize its own backbuffer. The wrapper leaves the game executable, Windows display modes, registry and drivers untouched. Its diagnostics are written locally.

I tested the approach successfully on two systems. This excerpt from my second system shows version 0.4 creating real backbuffers at both sizes in windowed mode.

```text
ResetEx: requested 5760x1080 Windowed=1 Hz=0
ResetEx result=0x00000000
ACTUAL BACKBUFFER: 5760x1080 format=22
ResetEx: requested 7680x1440 Windowed=1 Hz=0
ResetEx result=0x00000000
ACTUAL BACKBUFFER: 7680x1440 format=22
```

I write one diagnostic log per process under `%LOCALAPPDATA%\RaceRoomTripleHelper\d3d9-<pid>.log`. To help me troubleshoot, include your GPU, monitor layout, requested resolution and the relevant `CONFIG`, `BLOCKED`, `CreateDeviceEx` or `ResetEx` lines in a GitHub issue. The log can include local paths, so remove anything you prefer to keep private.

## Scope and compatibility

I built this as an unofficial, experimental wrapper for the 64-bit RaceRoom executable and tested it in local practice. I contacted the RaceRoom team to ask about online use and offered to share my findings. I’m still waiting for their response and can’t confirm multiplayer or game-protection compatibility.

The wrapped entry points reject exclusive fullscreen and adapter-group creation. Some native resources and swap chains are returned without wrappers, so this is only a partial COM wrapper. It exports the entry points listed in [`source/exports.def`](source/exports.def) and does not support proxy chaining. I haven’t verified wider GPU compatibility, overlays, HDR or VRR.

## Build from source

I build the Windows x64 DLL with Zig. From the repository root, run the build script in a POSIX shell with `zig` on your PATH.

```sh
sh build.sh
```

The script produces `build/d3d9.dll`. Set `ZIG_BIN` if you use a different path to Zig.
