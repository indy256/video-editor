param([ValidateSet('arm64', 'x64')][string]$Architecture = 'arm64')
$ErrorActionPreference = 'Stop'
& python "$PSScriptRoot/setup-tools.py" --arch $Architecture
if ($LASTEXITCODE -ne 0) { throw 'Helper setup failed' }
