# Generates assets\app.ico (16-256px PNG-compressed frames) from assets\AppIcon.png.
# Requires ffmpeg on PATH or at C:\ffmpeg\bin\ffmpeg.exe.
[CmdletBinding()]
param(
    [string]$Source = "",
    [string]$Output = ""
)

$ErrorActionPreference = 'Stop'

$scriptDir = $PSScriptRoot
if (-not $scriptDir) {
    $scriptDir = Split-Path -Parent ([IO.Path]::GetFullPath($MyInvocation.MyCommand.Definition))
}
if (-not $Source) { $Source = "$scriptDir\..\assets\AppIcon.png" }
if (-not $Output) { $Output = "$scriptDir\..\assets\app.ico" }
$Source = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Source)
$Output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
if (-not (Test-Path $Source)) { throw "Source not found: $Source" }

$ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
if ($ffmpeg) { $ffmpeg = $ffmpeg.Source } else { $ffmpeg = 'C:\ffmpeg\bin\ffmpeg.exe' }
if (-not (Test-Path $ffmpeg)) { throw "ffmpeg not found" }

$sizes = 16, 24, 32, 48, 64, 128, 256
$tmp = Join-Path $env:TEMP ("wr_icon_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp | Out-Null

try {
    $pngs = @()
    foreach ($s in $sizes) {
        $p = Join-Path $tmp "icon_$s.png"
        & $ffmpeg -y -loglevel error -i $Source -vf "scale=${s}:${s}:flags=lanczos" $p
        if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed for ${s}px" }
        $pngs += ,@($s, [IO.File]::ReadAllBytes($p))
    }

    $ms = New-Object IO.MemoryStream
    $bw = New-Object IO.BinaryWriter $ms

    # ICONDIR
    $bw.Write([uint16]0)            # reserved
    $bw.Write([uint16]1)            # type: icon
    $bw.Write([uint16]$pngs.Count)  # count

    $offset = 6 + 16 * $pngs.Count
    foreach ($entry in $pngs) {
        $size = $entry[0]; $data = $entry[1]
        $bw.Write([byte]($size -band 0xFF))          # width (256 -> 0)
        $bw.Write([byte]($size -band 0xFF))          # height
        $bw.Write([byte]0)                            # colors
        $bw.Write([byte]0)                            # reserved
        $bw.Write([uint16]1)                          # planes
        $bw.Write([uint16]32)                         # bpp
        $bw.Write([uint32]$data.Length)               # image size
        $bw.Write([uint32]$offset)                    # data offset
        $offset += $data.Length
    }
    foreach ($entry in $pngs) { $bw.Write($entry[1]) }
    $bw.Flush()

    [IO.File]::WriteAllBytes($Output, $ms.ToArray())
    Write-Host "Wrote $Output ($($ms.Length) bytes, $($pngs.Count) frames)"
} finally {
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
}
