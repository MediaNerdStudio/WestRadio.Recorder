# WestRadio Recorder

A slim, dynamic multitrack audio recorder for Windows. Record one input or twenty stereo pairs
at the same time, each to its own timestamped file, from any audio API Windows exposes:
MME, DirectSound, WASAPI, WDM-KS and ASIO.

Built with C++20, Qt 6 and PortAudio. FFmpeg is bundled for MP3 encoding, so the release
packages run on a clean machine without any extra installs.

![App icon](assets/AppIcon.png)

## Features

- **Any number of tracks** - add a channel strip per source; strips are vertical,
  Fairlight/Reaper style, in a horizontally scrolling console.
- **Input selection per track** - API type > audio interface > first physical channel.
  A `ST` toggle (on by default) records the selected channel plus the next one as a stereo pair;
  off records true mono.
- **Live meters** - armed tracks are monitored as soon as they have a device, before you press
  record. Segmented meters with peak hold, clip indicator and a dB scale.
- **Native sample rate** - every track records at the rate its device actually runs at
  (shown on the strip). ASIO opens only the channels you use and negotiates a supported rate.
- **Timestamped files** - `<TRACKNAME>_<YYYY-MM-DD>_<HHMMSS>.wav`, e.g.
  `VOICE_2026-09-21_120000.wav`.
- **WAV or MP3** - 32-bit float WAV, or MP3 at 48 kHz / 320 kbps with the track's channel count.
- **Configs** - save and load recorder setups (`*.wrrec.json`), pick one to load at startup,
  or pass `--config <path>` on the command line. Window size/position is remembered.
- **Many tracks on one device** - tracks sharing a device share one stream; a new track picks
  the next free channel on the previous track's device automatically.

## Download

Grab the latest release from the
[Releases](https://github.com/MediaNerdStudio/WestRadio.Recorder/releases) page:

| File | Use |
|---|---|
| `WestRadio.Recorder-<version>-setup.exe` | Installer (per-user or all-users, Start Menu shortcut, uninstaller) |
| `WestRadio.Recorder-<version>-win64.zip` | Portable - unzip anywhere and run `WestRadio.Recorder.exe` |

Requires Windows 10/11 x64. Nothing else needs to be installed.

## Quick start

1. Click the dashed `+` strip to add a track.
2. Click the input button on the strip and choose API > device; set the first channel and
   toggle `ST` for stereo or mono.
3. Name the track (bottom of the strip) and press `ARM`. The meter starts moving.
4. **Options** menu: choose WAV or MP3 and the output folder.
5. Press `● REC`; press `■ Stop` when done. Files appear in the output folder.
6. **File > Save** keeps the setup; **File > Use this config at startup** reloads it next time.

### ASIO notes

- ASIO is exclusive: close other software using the same driver first.
- PortAudio can only have **one** ASIO device open at a time. Several tracks on the same ASIO
  device are fine; two different ASIO devices are not.
- If a driver refuses to open, the error dialog shows the driver's own message (and it is written
  to `%LOCALAPPDATA%\WestRadio.Recorder\recorder.log`). Some drivers only run at the sample rate
  set in their own control panel.

## Building from source

Requirements:

- Visual Studio 2022+ Build Tools (C++ workload) and CMake 3.24+
- Qt 6.5+ MSVC x64 (tested with 6.11.2); set `QT_ROOT` if it is not at
  `C:\Qt\6.11.2\msvc2022_64`
- Node.js (only for the `npm run` shortcuts)

PortAudio and the ASIO SDK mirror are fetched automatically by CMake.

```powershell
npm run build:backend   # configure + build Release into build-msvc\Release
npm run dev             # run the app
```

For development builds FFmpeg is looked up next to the executable, then on `PATH`, then at
`C:\ffmpeg\bin\ffmpeg.exe`. Release packages ship their own copy.

### Headless tests

```powershell
.\build-msvc\Release\RecorderTest.exe 3000            # record 3 s from the default device
.\build-msvc\Release\RecorderTest.exe 3000 ASIO       # filter by API (and optionally device / channel)
.\build-msvc\Release\RecorderTest.exe monitor         # monitor -> record -> monitor cycle
.\build-msvc\Release\RecorderTest.exe config          # config save/load round-trip
.\build-msvc\Release\RecorderTest.exe ffmpeg          # locate FFmpeg and encode a test MP3
```

## Making a release

The version lives in `CMakeLists.txt` (`project(... VERSION x.y.z)`) and must match
`package.json`; it flows into the EXE version resource, the window title, the About dialog and
the package names.

```powershell
npm run package
```

This builds Release, runs `windeployqt`, adds the MSVC runtime, downloads FFmpeg once into
`tools\ffmpeg\` (override with `$env:FFMPEG_EXE`), writes `dist\WestRadio.Recorder-<version>-win64.zip`
and, if [Inno Setup 6](https://jrsoftware.org/isinfo.php) is installed, builds
`dist\WestRadio.Recorder-<version>-setup.exe`. Then tag and publish:

```powershell
git tag -a vX.Y.Z -m "WestRadio Recorder X.Y.Z"
git push --tags
gh release create vX.Y.Z dist\*-setup.exe dist\*-win64.zip --title "WestRadio Recorder X.Y.Z"
```

## Project layout

```
src/            Qt application (MainWindow, main.cpp)
src/core/       AudioEngine, DeviceStream (PortAudio), AudioTrack, WavStreamWriter,
                FfmpegTask, RecorderConfig
src/ui/         TrackWidget (channel strip), MeterWidget
tests/          RecorderTest headless checks
assets/         App icon (PNG / ICO), Qt and Windows resource files
installer/      Inno Setup script
scripts/        build-backend.ps1, dev.ps1, package.ps1, make-icon.ps1
```

## Licenses

WestRadio Recorder bundles third-party components; see `LICENSES.txt` in each package.

- Qt 6 - LGPL v3 (dynamically linked)
- PortAudio - MIT
- Steinberg ASIO SDK - Steinberg ASIO licensing agreement
- FFmpeg - GPL v3 (gyan.dev essentials build, shipped as a separate executable and invoked as a
  separate process)
