$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$qtRoot = if ($env:QT_ROOT) { $env:QT_ROOT } else { 'C:\Qt\6.11.2\msvc2022_64' }

$env:Path = "$qtRoot\bin;$env:Path"

$vsPrompt = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vsPrompt)) {
    Write-Error "Visual Studio C++ build tools not found at $vsPrompt"
}

cmd /c "`"$vsPrompt`" && cmake -S `"$root`" -B `"$root\build-msvc`" -G `"Visual Studio 18 2026`" -A x64 -DCMAKE_PREFIX_PATH=`"$qtRoot`""
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmd /c "`"$vsPrompt`" && cmake --build `"$root\build-msvc`" --config Release"
exit $LASTEXITCODE
