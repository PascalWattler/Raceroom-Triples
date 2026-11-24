# RaceRoom Triples without NVIDIA Surround or AMD Eyefinity

I built this 64-bit Direct3D 9 wrapper to use RaceRoom’s built-in triple-screen projection across three independent monitors. I wanted to keep my five-monitor Windows desktop layout without enabling NVIDIA Surround. RaceRoom handles the triple projection and FOV. I use the DLL on its own, without a separate window-resizing tool or any other additional 3rd-party-tool. 

If it saves you some trouble and you like it, you can [buy me a coffee](https://buymeacoffee.com/waddel).

> [!WARNING]
> **Use at your own risk.** This is an unofficial tool with no approval from KW Studios, the developer of RaceRoom. I asked them about it and have not received an answer. I use the wrapper myself, including online, and have had no problems so far. I see it in the same category as ReShade and similar tools that are tolerated, but that is my opinion and not a promise. I give no guarantee and accept no liability for bans, lost progress or any other consequence of using it.

## What you need

- The 64-bit version of RaceRoom on Steam. The wrapper runs the game on Direct3D 9 or, if you ask for it in the INI, on the DXVK that RaceRoom ships (see [DXVK](#dxvk)).
- Three monitors that Windows uses as an extended desktop. On my rig the centre monitor is the Windows main display, and the game window extends from it to the left and right.
- RaceRoom set to **Borderless Window**. Exclusive fullscreen does not work with the wrapper.
- No other `d3d9.dll` in the game folder. I don’t support combining this wrapper with another local D3D9 proxy, such as ReShade or a DXVK you copied there yourself.

## Install

1. **Before you install anything, start RaceRoom and set the display mode to Borderless Window.** Apply the setting and exit the game completely. If you skip this, the game may fail to start with the wrapper in place (see [Troubleshooting](#troubleshooting)).
2. Download the ZIP of the latest version from [Releases](https://github.com/PascalWattler/Raceroom-Triples/releases) and unpack it.
3. In Steam, open RaceRoom’s **Manage → Browse local files** and go to `Game\x64`. This is the folder that contains `RRRE64.exe`. `Game` and `Game\x64` are different folders.
4. If there is already a `d3d9.dll` in that folder, find out what it belongs to before you go on. Don’t overwrite a DLL from another tool.
5. Copy `d3d9.dll` and `RaceRoomTriple.ini` from the ZIP into that folder. Don’t copy anything into the Windows folder.
6. Start RaceRoom. In the video settings, select the **48:9** aspect ratio and the combined resolution that matches your screens.
7. Enable RaceRoom’s triple-screen projection and set the screen geometry and FOV in the game.

| Setup | Combined resolution | Test status |
| --- | --- | --- |
| 3 × 1920 × 1080 | 5760 × 1080 | Back buffer creation verified on my second system |
| 3 × 2560 × 1440 | 7680 × 1440 | Triple projection tested in the game on my rig |
| 3 × 3840 × 2160 | 11520 × 2160 | Back buffer creation verified outside the game. A user reported it working in RaceRoom; I haven’t tested it there myself |
| 2560 × 1080 between 2 × 1920 × 1080 | 6400 × 1080 | A user reported it working in RaceRoom, with `StrictTripleAspect = false` (see [Configure](#configure-resolutions-and-refresh-entries)). I verified back buffer creation outside the game |

The refresh rates in the game’s list are menu entries. Windows and the graphics driver still control each monitor’s actual refresh rate.

## Troubleshooting

**The game shows a D3D9 “unsupported” error at startup.** RaceRoom was last saved in exclusive fullscreen. The log then shows `Windowed=0` followed by `BLOCKED: exclusive fullscreen`. Close the game, move `d3d9.dll` out of `Game\x64` for a moment (or rename it), start the game, switch to Borderless Window and exit. Put the DLL back and start again.

**The new resolutions don’t appear.** Check that the DLL is in `Game\x64` next to `RRRE64.exe` (the x64 is important!), and that you restarted the game after copying it. Start the game the normal way: Steam’s DXVK launch option runs it from another folder, where the wrapper is not loaded (see [DXVK](#dxvk)).

**AMD Radeon RX 5000 or newer: freeze, black screen or crash.** On Direct3D 9, RaceRoom can freeze or crash on these cards when MSAA and motion blur are both enabled. This happens no matter if the wrapper is being used or not. I don’t have an AMD card; this comes from reports in the RaceRoom community. There are two ways around it: disable motion blur (or MSAA) in the video settings before you change the resolution, or switch the wrapper to [DXVK](#dxvk), which according to the same reports does not have the problem. I could not test the wrapper with DXVK on an AMD card.

**The game freezes once when you select the new resolution.** One user reported a freeze the first time the resolution was changed, and that it worked after that. I have not seen this on my systems. But if it freezes, give it a second try, just to be sure. 

**Windows or your virus scanner warns about the DLL.** The DLL is not digitally signed. Each release lists the SHA256 checksum of the ZIP, so you can check that your download is the file I published.

**Where to find the log.** The wrapper writes one log per game start to `%LOCALAPPDATA%\RaceRoomTripleHelper\d3d9-<pid>.log`. Nothing is sent anywhere. Old logs are not removed automatically; you can delete the folder’s contents at any time.

**Reporting a problem.** Open a [GitHub issue](https://github.com/PascalWattler/Raceroom-Triples/issues) with your GPU, monitor layout, the resolution you selected and the `CONFIG`, `BLOCKED`, `CreateDeviceEx` and `ResetEx` lines from the log. The log contains the path of your game folder, so remove anything you prefer to keep private.

## Uninstall

Close RaceRoom and remove the `d3d9.dll` you installed, along with `RaceRoomTriple.ini`. If the game has saved the triple resolution, select a standard resolution in the game before you remove the DLL.

## Configure resolutions and refresh entries

The wrapper reads `RaceRoomTriple.ini` from the DLL’s folder when the game starts. The ZIP contains this example. Save it as plain text (UTF-8) under that exact name, and restart RaceRoom after changes.

```ini
[Settings]
StrictTripleAspect = true
Renderer = dx9

[Modes]
7680x1440 = 60,75,100,120,144
5760x1080 = 60,75,100,120,144
11520x2160 = 60,75,100,120,144
```

Each row under `[Modes]` is `WIDTHxHEIGHT = Hz,Hz,...`, where width is the total across all three screens.

With `StrictTripleAspect = true`, the combined size must be exactly 48:9. Set it to `false` for custom ratios between 1:1 and 12:1, such as a bezel-corrected width.

`Renderer` is `dx9` or `dxvk` and is explained under [DXVK](#dxvk). Without the line, the wrapper uses `dx9`.

The INI is optional. If it is missing, unreadable or contains no valid mode, the wrapper offers the three resolutions above at 60 and 144 Hz. Invalid rows are skipped and named in the log with their line number; valid rows still count. Duplicates are ignored.

| Limit | Value |
| --- | --- |
| Width | 320 to 16384 pixels |
| Height | 200 to 16384 pixels |
| Refresh entries | 24 to 1000 Hz, up to 64 per row |
| Distinct modes | 256 |
| File size | 64 KiB |

The wrapper also leaves out modes that exceed the texture size limit your graphics adapter reports. RaceRoom and the driver still have to create the render surfaces successfully.

## DXVK

RaceRoom ships [DXVK](https://github.com/doitsujin/dxvk), which translates Direct3D 9 to Vulkan, in the folder `Game\x64dxvk`. Steam’s DXVK launch option starts the game from that folder. The wrapper is located in `Game\x64` and is not loaded then, so the triple resolutions are missing.

To use both together:

1. Install the wrapper in `Game\x64` as described above.
2. Set `Renderer = dxvk` under `[Settings]` in `RaceRoomTriple.ini`.
3. Start RaceRoom the normal way, not through Steam’s DXVK launch option.

(IMPORTANT: The RaceRoomTriple.ini is the only place to enable DXVK if you want to use this wrapper!)

The wrapper then forwards the game’s calls to `Game\x64dxvk\d3d9.dll` instead of the Direct3D 9 of Windows. It changes nothing in `Game\x64dxvk`, so a game update that brings a newer DXVK is used without reinstalling anything.

To check that DXVK is in use, look for `RENDERER active=dxvk` in the wrapper’s log. DXVK also writes its own log, `Game\RRRE64_d3d9.log`, on every start. If DXVK cannot be loaded, the game starts on Direct3D 9 and the wrapper’s log says `RENDERER active=dx9 (fallback, DXVK was requested)`, with the reason in the line above it.

I tested this on my rig at 7680 × 1440 with an RTX 5080 and the DXVK 2.4 that RaceRoom shipped in November 2026. The picture and the triple projection were the same as on Direct3D 9. The NVIDIA overlay showed no FPS value in this mode. I have not tested it on AMD or Intel cards. If you run it on one, please tell me how it goes in a [GitHub issue](https://github.com/PascalWattler/Raceroom-Triples/issues).

## How it works

My first attempts to resize the window stretched the existing image. RaceRoom also reverted a manually configured 7680 × 1440 resolution to 1024 × 768. That behaviour led me to try adding the requested sizes to the adapter mode list that RaceRoom reads.

The wrapper loads the real Windows `d3d9.dll` by its absolute system path, or DXVK’s `d3d9.dll` from `Game\x64dxvk` if the INI says `Renderer = dxvk`, and forwards calls through COM wrappers for `IDirect3D9/Ex` and `IDirect3DDevice9/Ex`. It appends the configured entries to the mode enumeration returned by `GetAdapterModeCount`, `EnumAdapterModes` and their Ex variants. RaceRoom then selects a mode and asks Direct3D to create or resize its own back buffer. The wrapper leaves the game executable, Windows display modes, registry and drivers untouched.

This log excerpt from my second system shows real back buffers being created at both sizes in windowed mode.

```text
ResetEx: requested 5760x1080 Windowed=1 Hz=0
ResetEx result=0x00000000
ACTUAL BACKBUFFER: 5760x1080 format=22
ResetEx: requested 7680x1440 Windowed=1 Hz=0
ResetEx result=0x00000000
ACTUAL BACKBUFFER: 7680x1440 format=22
```

## Scope and compatibility

This is an experimental wrapper for the 64-bit RaceRoom executable only.

- The wrapped entry points reject exclusive fullscreen and adapter-group devices.
- Some native resources and swap chains are returned without wrappers, so this is a partial COM wrapper.
- It exports the entry points listed in [`source/exports.def`](source/exports.def) and forwards only to the Direct3D 9 of Windows or to the DXVK in `Game\x64dxvk`. It does not chain to other proxies.
- I haven’t verified wider GPU compatibility, overlays or VRR.

## Build from source

The DLL is built with [Zig](https://ziglang.org/) as the C++ compiler; I use version 0.15.2. Put `zig` on your PATH or set `ZIG_BIN` to its path. From the repository root, run one of these:

```powershell
.\build.ps1
```

```sh
sh build.sh
```

Both scripts run the parser tests and produce `build/d3d9.dll`. The PowerShell script then runs a smoke test: it loads the fresh DLL, reads the mode list, creates a windowed Direct3D device and resizes it to 5760 × 1080, all without the game. Pass `-NoSmokeTest` on a machine without a graphics adapter.

`.\package.ps1` does the same and then puts the release ZIP together in `build`, with its SHA256 checksum next to it.

## License

[MIT](LICENSE). The DLL contains runtime code from Zig, MinGW-w64 and LLVM; their license texts are included in the release ZIP.

## Support

If the wrapper is useful to you and you like it: [buymeacoffee.com/waddel](https://buymeacoffee.com/waddel).
