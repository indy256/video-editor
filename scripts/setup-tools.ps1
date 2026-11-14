param([ValidateSet('arm64', 'x64')][string]$Architecture = 'arm64')
$ErrorActionPreference = 'Stop'
$toolDir = Join-Path (Split-Path $PSScriptRoot -Parent) 'tools'
New-Item -ItemType Directory -Path $toolDir -Force | Out-Null
function Get-VerifiedAsset($repository, $name, $destination) {
    $release = Invoke-RestMethod "https://api.github.com/repos/$repository/releases/latest"
    $asset = $release.assets | Where-Object name -eq $name
    if (-not $asset -or $asset.digest -notmatch '^sha256:([a-fA-F0-9]{64})$') { throw "No SHA-256 digest for $repository/$name" }
    $expected = $matches[1]
    $download = "$destination.download"
    Invoke-WebRequest -UseBasicParsing $asset.browser_download_url -OutFile $download
    if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash -ne $expected) { throw "Checksum failed: $name" }
    Move-Item -LiteralPath $download -Destination $destination -Force
    Write-Host "$repository $($release.tag_name): SHA-256 verified"
}
$ytAsset = if ($Architecture -eq 'arm64') { 'yt-dlp_arm64.exe' } else { 'yt-dlp.exe' }
$denoArch = if ($Architecture -eq 'arm64') { 'aarch64' } else { 'x86_64' }
Get-VerifiedAsset 'yt-dlp/yt-dlp' $ytAsset "$toolDir\yt-dlp.exe"
Get-VerifiedAsset 'denoland/deno' "deno-$denoArch-pc-windows-msvc.zip" "$toolDir\deno.zip"
Expand-Archive -LiteralPath "$toolDir\deno.zip" -DestinationPath $toolDir -Force
Write-Host 'Place ffmpeg.exe on PATH or in tools, then run build.ps1.'
