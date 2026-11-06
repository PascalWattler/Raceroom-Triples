# Raceroom-Triples
Raceroom Triples without NVIDIA Surround with a D3D9 wrapper DLL. 

## Problem and observation

Normal Game/x64/RRRE64.exe, Direct3D 9, no DXVK. Three independent 2560x1440 monitors, RTX 5080, additional independent monitors present. NVIDIA Surround is unwanted because of desktop configuration changes. External window resizing alone stretched a small render image. Prepared 7680x1440 configuration reverted to 1024x768.

Installing the local prototype d3d9.dll next to the actual executable enabled me to select the 48:9 resolution and configure the existing in-game triple projection. No separate window helper is necessary. A supplied log now confirms:

```
CreateDeviceEx: requested 1024x768 Windowed=1 Hz=0
ACTUAL BACKBUFFER: 1024x768 format=22
ResetEx: requested 7680x1440 Windowed=1 Hz=0
ResetEx result=0x00000000
ACTUAL BACKBUFFER: 7680x1440 format=22
```

The inference is a coupling of accepted windowed dimensions to physical-adapter mode enumeration. No game source inspection or identification of a specific internal function has been performed.

## Implementation

Local d3d9 proxy -> actual system d3d9 loaded by absolute system path. COM wrappers around IDirect3D9/Ex and IDirect3DDevice9/Ex. Append mode entries in GetAdapterModeCount/Ex and EnumAdapterModes/Ex. Keep the two 7680x1440 entries first, then append 5760x1080 and 11520x2160, each at 60 and 144 Hz. Backbuffer requests are not forced or upscaled. No Windows display modes, EDID, registry or driver changes.

The six entries are offered for eligible native adapter/format queries without monitor detection or GPU/engine size filtering. Device creation can still fail; 4K functionality must be tested. Fullscreen and adapter-group creation are rejected at the wrapped entry points, resulting in an error message. Native resources can expose the native device, so this is not a comprehensive fullscreen guard. Switch to windowed mode before installing the wrapper. 

## Evidence and limitations

1080p/4K, overlays, HDR/VRR, broad GPU compatibility and online protection approval are unconfirmed.

Known code limitations: unwrapped native resources/swapchains and GetDevice identity escape; unsupported QueryInterface requests rejected; only twelve named exports; no proxy chaining; frame sampling/reset/threading needs review. Existing COM forwarding code retained to minimize regression scope.

## Update Configuration instead of fixed modes

Read RaceRoomTriple.ini from the proxy DLL directory using GetModuleHandleExW FROM_ADDRESS + GetModuleFileNameW, never the current working directory. Read-only Win32 file loading; bounded 64-KiB UTF-8 input. Read once in InitOnce initialization before enumeration; no live reload. Config diagnostics are logged after InitOnce completes to avoid recursive initialization.

[Settings] StrictTripleAspect=true requires exact width*9 == height*48; false allows custom ratios within bounded dimensions. [Modes] uses WIDTHxHEIGHT = comma-separated refresh values. Bounds: 320..16384 by 200..16384, <=134217728 pixels; custom aspect width/height 1..12; refresh 24..1000; <=256 distinct triples and <=64 values/line; line length <512 bytes. Values represent windowed game-mode metadata, not physical timings.

Allocation-free portable parser; exact numeric syntax, overflow checks, malformed-line diagnostics, duplicate elimination, UTF-8 BOM, comments and CRLF. Valid rows survive other invalid rows. Empty/absent/encoding-invalid/unreadable files fall back to the previous six presets. Settings are parsed before mode validation independent of section order.

Per-object immutable adapter selections filter validated modes using reported HAL MaxTextureWidth/Height for up to 32 adapters. Failed/unknown caps do not prove support and leave otherwise valid modes offered, with logs. Conservative texture-size filtering is not a complete backbuffer/render-pipeline capability validation. No automatic desktop topology detection.

Export table and DeviceProxy block unchanged. New source: config.h, policy.h, loader/diagnostics, D3DProxy table mapping. Portable parser/policy tests pass, including sanitizer malformed-input exercise; leak testing unavailable in sandbox. Windows game execution still untested for 0.4. Earlier sections describing 0.3 remain the historical comparison; this update supersedes their hardcoded-table description.
