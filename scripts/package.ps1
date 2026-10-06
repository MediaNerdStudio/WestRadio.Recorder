# Builds Release and packages WestRadio.Recorder into dist\<name>-win64.zip.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# Single-source version check: CMake project() must match package.json.
$pkg = Get-Content "$root\package.json" -Raw | ConvertFrom-Json
$cmake = Select-String -Path "$root\CMakeLists.txt" `
    -Pattern 'project\(WestRadio\.Recorder VERSION ([0-9]+\.[0-9]+\.[0-9]+)' | Select-Object -First 1
if (-not $cmake) { throw "Could not read version from CMakeLists.txt" }
$cmakeVersion = $cmake.Matches[0].Groups[1].Value
if ($cmakeVersion -ne $pkg.version) {
    throw "Version mismatch: CMakeLists.txt=$cmakeVersion, package.json=$($pkg.version)"
}
$version = $pkg.version
Write-Host "Packaging version $version"

# Build.
& powershell -NoProfile -ExecutionPolicy Bypass -File "$root\scripts\build-backend.ps1"
if ($LASTEXITCODE -ne 0) { throw "build-backend failed ($LASTEXITCODE)" }

$qtRoot = if ($env:QT_ROOT) { $env:QT_ROOT } else { 'C:\Qt\6.11.2\msvc2022_64' }
$windeployqt = "$qtRoot\bin\windeployqt.exe"
if (-not (Test-Path $windeployqt)) { throw "windeployqt not found at $windeployqt" }

$exe = "$root\build-msvc\Release\WestRadio.Recorder.exe"
if (-not (Test-Path $exe)) { throw "Executable not found: $exe" }

$stageDir = "$root\dist\WestRadio.Recorder-$version-win64"
$zip = "$stageDir.zip"
if (Test-Path $stageDir) { Remove-Item $stageDir -Recurse -Force }
if (Test-Path $zip) { Remove-Item $zip -Force }
New-Item -ItemType Directory -Path $stageDir | Out-Null

Copy-Item $exe $stageDir
& $windeployqt --release --no-translations --no-opengl-sw --no-system-d3d-compiler `
    --compiler-runtime "$stageDir\WestRadio.Recorder.exe"
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed ($LASTEXITCODE)" }

# --compiler-runtime often can't locate the VC redist folder with VS Build
# Tools; copy the x64 CRT DLLs ourselves if the exe still lacks them.
if (-not (Test-Path "$stageDir\msvcp140.dll")) {
    $crt = Get-ChildItem 'C:\Program Files*\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT' `
        -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
    if ($crt) {
        Copy-Item "$($crt.FullName)\*.dll" $stageDir
        Write-Host "Copied MSVC CRT from $($crt.FullName)"
    } else {
        Write-Warning "MSVC CRT redist not found; package requires VC++ runtime on target machine"
    }
}

# Provision FFmpeg so the app is fully self-contained (cached in tools/ffmpeg).
$toolsDir = "$root\tools\ffmpeg"
New-Item -ItemType Directory -Path $toolsDir -Force | Out-Null
if ($env:FFMPEG_EXE -and (Test-Path $env:FFMPEG_EXE)) {
    Copy-Item $env:FFMPEG_EXE "$toolsDir\ffmpeg.exe" -Force
    $licSrc = Join-Path (Split-Path -Parent $env:FFMPEG_EXE) 'LICENSE'
    if (Test-Path $licSrc) { Copy-Item $licSrc "$toolsDir\LICENSE.txt" -Force }
    Write-Host "Using FFMPEG_EXE override: $env:FFMPEG_EXE"
} elseif (-not (Test-Path "$toolsDir\ffmpeg.exe")) {
    $url = 'https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip'
    $dl = "$env:TEMP\ffmpeg-release-essentials.zip"
    Write-Host "Downloading $url"
    Invoke-WebRequest -Uri $url -OutFile $dl
    $extractDir = "$env:TEMP\ffmpeg-essentials-extract"
    if (Test-Path $extractDir) { Remove-Item $extractDir -Recurse -Force }
    Expand-Archive -Path $dl -DestinationPath $extractDir
    $bin = Get-ChildItem $extractDir -Recurse -Filter ffmpeg.exe | Select-Object -First 1
    if (-not $bin) { throw "ffmpeg.exe not found in downloaded archive" }
    Copy-Item $bin.FullName "$toolsDir\ffmpeg.exe"
    $lic = Get-ChildItem $extractDir -Recurse -Include LICENSE,LICENSE.txt,GPLv3.txt -File |
        Select-Object -First 1
    if ($lic) { Copy-Item $lic.FullName "$toolsDir\LICENSE.txt" }
    & "$toolsDir\ffmpeg.exe" -version | Select-Object -First 1 |
        Set-Content "$toolsDir\VERSION.txt" -Encoding UTF8
    Remove-Item $dl, $extractDir -Recurse -Force -ErrorAction SilentlyContinue
}
if (-not (Test-Path "$toolsDir\ffmpeg.exe")) { throw "Failed to provision ffmpeg.exe" }
if (-not (Test-Path "$toolsDir\LICENSE.txt")) {
    Set-Content "$toolsDir\LICENSE.txt" -Encoding UTF8 `
        'FFmpeg: GPL v3 (gyan.dev essentials build). Source: https://ffmpeg.org'
}
New-Item -ItemType Directory -Path "$stageDir\ffmpeg" -Force | Out-Null
Copy-Item "$toolsDir\ffmpeg.exe" "$stageDir\ffmpeg\ffmpeg.exe"
Copy-Item "$toolsDir\LICENSE.txt" "$stageDir\ffmpeg\LICENSE.txt"
Write-Host "Bundled ffmpeg.exe"

@'
WestRadio Recorder
==================

Native Windows multitrack audio recorder (Qt 6 + PortAudio, ASIO support).

Requirements
------------
- Windows 10/11 x64
- FFmpeg is bundled (ffmpeg\ffmpeg.exe) - MP3 encoding works out of the box.

Running
-------
WestRadio.Recorder.exe              - normal start
WestRadio.Recorder.exe --config X   - load config file X (*.wrrec.json)
File > "Use this config at startup" stores the config to load every launch.

Logs and settings
-----------------
- Log:  %LOCALAPPDATA%\WestRadio\WestRadio Recorder\recorder.log
- Settings / startup config: registry HKCU\Software\WestRadio\Recorder

Each armed track is written as a 32-bit float WAV named
<TRACK>_<YYYY-MM-DD_HHMMSS>.wav in the chosen output folder.
'@ | Set-Content -Path "$stageDir\README.txt" -Encoding UTF8

@'
Third-party licenses
====================

Qt 6 (QtCore / QtGui / QtWidgets)
    GNU Lesser General Public License v3 (LGPLv3).
    https://www.qt.io/ and https://doc.qt.io/qt-6/lgpl.html
    Qt is linked dynamically; the Qt runtime DLLs are bundled by windeployqt.

PortAudio v19.7.0
    MIT-style PortAudio license.
    http://www.portaudio.com/license.html
    https://github.com/PortAudio/portaudio

Steinberg ASIO SDK (mirror: github.com/audiosdk/asio)
    Used at build time to enable the ASIO host API inside PortAudio.
    Steinberg ASIO licensing agreement; see the SDK for details.

FFmpeg
    Bundled as ffmpeg\ffmpeg.exe (gyan.dev "essentials" build, GPL v3).
    Invoked by WestRadio Recorder as a separate process for MP3 encoding.
    Build version is recorded in tools\ffmpeg\VERSION.txt of the source repo.
    https://ffmpeg.org/legal.html - source: https://ffmpeg.org
'@ | Set-Content -Path "$stageDir\LICENSES.txt" -Encoding UTF8

Compress-Archive -Path "$stageDir\*" -DestinationPath $zip -CompressionLevel Optimal
Write-Host "Wrote $zip"

# Inno Setup installer (optional; skipped if ISCC.exe is not found).
$iscc = if ($env:ISCC) { $env:ISCC } else { $null }
if (-not $iscc -or -not (Test-Path $iscc)) {
    $candidates = @(
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
        'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
        'C:\Program Files\Inno Setup 6\ISCC.exe'
    )
    $iscc = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $iscc) {
        $cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue
        if ($cmd) { $iscc = $cmd.Source }
    }
}
$iss = "$root\installer\WestRadio.Recorder.iss"
if ($iscc -and (Test-Path $iss)) {
    Write-Host "Building installer with $iscc"
    & $iscc "/DAppVersion=$version" /Qp $iss
    if ($LASTEXITCODE -ne 0) { throw "ISCC failed ($LASTEXITCODE)" }
} else {
    Write-Warning "Inno Setup (ISCC.exe) not found; skipping installer build"
}
