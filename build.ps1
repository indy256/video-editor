param(
    [string]$QtRoot = 'C:\Qt\6.11.2\msvc2022_arm64',
    [string]$ToolsRoot = 'C:\Qt\Tools',
    [switch]$Test
)
$ErrorActionPreference = 'Stop'
$cmake = Join-Path $ToolsRoot 'CMake\bin\cmake.exe'
$ninja = Join-Path $ToolsRoot 'Ninja\ninja.exe'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.ARM64 -property installationPath
if (-not $vs) { $vs = & $vswhere -latest -products '*' -property installationPath }
if (-not $vs) { throw 'Visual Studio C++ tools were not found.' }
$arch = if ($QtRoot -match 'arm64') { 'arm64' } else { 'amd64' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
# Import the compiler environment; do not rely on a PATH command named cl.
$environment = & $env:ComSpec /d /c "call `"$vcvars`" $arch >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the C++ toolchain.' }
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$compiler = Join-Path $env:VCToolsInstallDir "bin\Host$arch\$arch\cl.exe"
$env:PATH = "$QtRoot\bin;$PSScriptRoot\tools;$env:PATH"
$buildDir = Join-Path $PSScriptRoot 'build'
& $cmake -S $PSScriptRoot -B $buildDir -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_CXX_COMPILER=$compiler" "-DCMAKE_PREFIX_PATH=$QtRoot" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'Configure failed.' }
& $cmake --build $buildDir --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
if ($Test) {
    & (Join-Path $ToolsRoot 'CMake\bin\ctest.exe') --test-dir $buildDir --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        Get-Content (Join-Path $buildDir 'test-results.txt') -ErrorAction SilentlyContinue
        throw 'Tests failed.'
    }
}
$dist = Join-Path $PSScriptRoot 'dist'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$deployedExe = Join-Path $dist 'VideoEditor.exe'
try { Copy-Item -LiteralPath "$buildDir\VideoEditor.exe" -Destination $deployedExe -Force }
catch {
    # Windows can rename a running executable while retaining its mapped image.
    # Preserve that build so the open player and its selection can keep running.
    $previousName = 'VideoEditor.previous-' + [Guid]::NewGuid().ToString('N') + '.exe'
    Rename-Item -LiteralPath $deployedExe -NewName $previousName
    try { Copy-Item -LiteralPath "$buildDir\VideoEditor.exe" -Destination $deployedExe -Force }
    catch { Rename-Item -LiteralPath (Join-Path $dist $previousName) -NewName 'VideoEditor.exe'; throw }
    Write-Host "Previous running build retained at $dist\$previousName"
}
& "$QtRoot\bin\windeployqt.exe" --release --compiler-runtime "$dist\VideoEditor.exe"
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
New-Item -ItemType Directory -Force -Path "$dist\tools" | Out-Null
foreach ($tool in @('ffmpeg', 'yt-dlp', 'deno')) {
    $localTool = Join-Path $PSScriptRoot "tools\$tool.exe"
    if (Test-Path -LiteralPath $localTool) { Copy-Item -LiteralPath $localTool -Destination "$dist\tools" -Force }
    else {
        $command = Get-Command "$tool.exe" -ErrorAction SilentlyContinue
        if ($command) { Copy-Item -LiteralPath $command.Source -Destination "$dist\tools" -Force }
        else { Write-Warning "$tool not found. Add it to dist\tools before using the app." }
    }
}
Write-Host "Ready: $dist\VideoEditor.exe"
