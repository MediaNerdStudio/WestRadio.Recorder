$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$qtRoot = if ($env:QT_ROOT) { $env:QT_ROOT } else { 'C:\Qt\6.11.2\msvc2022_64' }
$env:Path = "$qtRoot\bin;$env:Path"

$executable = "$root\build-msvc\Release\WestRadio.Recorder.exe"
if (-not (Test-Path $executable)) {
    & "$PSScriptRoot\build-backend.ps1"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

& $executable
