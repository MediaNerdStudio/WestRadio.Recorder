# WestRadio.Recorder

Native Windows multitrack audio recorder built with C++20, Qt 6 and PortAudio.

## Requirements

- Windows 10/11 x64
- Qt 6.5+ (tested with 6.11.2 MSVC 2022 64-bit)
- Visual Studio 2022 / Build Tools C++ workload
- CMake 3.24+
- FFmpeg on PATH or at `C:\ffmpeg\bin\ffmpeg.exe` (used for MP3 encoding and the combined multi-channel WAV)

The CMake build downloads PortAudio and a mirror of the Steinberg ASIO SDK automatically via `FetchContent`, so ASIO support is included without any manual SDK steps.

## Build

```powershell
npm run build:backend
```

Or directly with PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-backend.ps1
```

Set `QT_ROOT` if Qt is installed somewhere other than `C:\Qt\6.11.2\msvc2022_64`.

A headless recorder test binary is also built:

```powershell
.\build-msvc\Release\RecorderTest.exe 3000
```

## Run

```powershell
npm run dev
```

## How it works

- Each track selects a Windows audio API (MME, DirectSound, WASAPI, WDM-KS, ASIO), device, mono/stereo mode and a physical channel offset.
- A single PortAudio stream is opened per device; all tracks using the same device share that stream.
- ASIO streams open only the channel range the tracks need (PortAudio channel selectors), negotiate a supported sample rate (device default first, then 48k/44.1k/96k/88.2k/192k) and fall back to full-duplex if input-only fails. PortAudio allows only one ASIO device open at a time.
- Track WAV writers are created after the stream has settled on its sample rate, so the WAV header always matches the capture rate.
- Armed tracks are monitored live: the engine keeps input streams open (no writers) so meters move before recording; monitoring restarts automatically after a recording stops. Monitoring errors go to the status bar, not a dialog.
- All PortAudio start/stop calls happen on the GUI thread: ASIO drivers are COM objects bound to the thread that initialised PortAudio, so opening them from a worker thread fails with "Unanticipated host error".
- `RecorderTest.exe monitor` exercises the monitoring → record → monitoring cycle headlessly.
- Recording-start failures are reported with the PortAudio error text in a dialog; warnings are also appended to `%LOCALAPPDATA%\WestRadio.Recorder\recorder.log`.
- UI is a dark Fairlight-style console: vertical 96px channel strips with segmented meters and a dB scale; `RecorderTest.exe <ms> [apiFilter] [deviceFilter] [channelOffset]` for headless checks.
- Every track writes a 32-bit float WAV file named `<TRACK>_<YYYY-MM-DD_HHMMSS>.wav`.
- If MP3 is selected, the WAV is transcoded to MP3 with FFmpeg and the WAV is removed.
- If "Also create one combined multi-channel WAV" is selected, FFmpeg `amerge` interleaves all track WAVs into a single Wave64 (`.wav`) file.
