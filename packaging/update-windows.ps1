param([Parameter(Mandatory=$true)][string]$PlanPath)
$ErrorActionPreference = 'Stop'
$stage = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($PlanPath))
# Only clean the staging directory containing this helper and its plan.
$stageDirectory = Get-Item -LiteralPath $stage -Force
if ($stageDirectory.FullName -ne [IO.Path]::GetFullPath($PSScriptRoot) -or
    $stageDirectory.Name -notlike '.video-editor-update-*' -or
    ($stageDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'Invalid staging directory.'
}
Set-Location -LiteralPath ([IO.Path]::GetDirectoryName($stage))
$replaced = $false
function Quote-Argument([string]$value) {
    $escaped = [regex]::Replace($value, '(\\*)"', '$1$1\"')
    '"' + ([regex]::Replace($escaped, '(\\+)$', '$1$1')) + '"'
}
function Restart-Editor {
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $target
    $start.WorkingDirectory = [IO.Path]::GetDirectoryName($target)
    $start.UseShellExecute = $false
    $start.Arguments = (@($plan.arguments | ForEach-Object { Quote-Argument $_ }) -join ' ')
    $null = [Diagnostics.Process]::Start($start)
}
try {
    $plan = Get-Content -LiteralPath $PlanPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $target = [IO.Path]::GetFullPath($plan.target)
    $source = [IO.Path]::GetFullPath($plan.source)
    if ([IO.Path]::GetDirectoryName($source) -ne $stage -or
        [IO.Path]::GetDirectoryName($stage) -ne [IO.Path]::GetDirectoryName($target) -or
        !(Test-Path -LiteralPath $target -PathType Leaf) -or
        !(Test-Path -LiteralPath $source -PathType Leaf)) { throw 'Invalid update paths.' }
    $editorProcess = Get-Process -Id $plan.pid -ErrorAction Stop
    [IO.File]::WriteAllText((Join-Path $stage 'ready'), 'ready')
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while (!(Test-Path -LiteralPath (Join-Path $stage 'commit'))) {
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Installation was not committed.' }
        Start-Sleep -Milliseconds 100
    }
    if (!$editorProcess.WaitForExit(120000)) { throw 'Video Editor did not close in time.' }
    # The portable launcher's parent process exits just after the player does.
    for ($attempt = 0; $attempt -lt 40; $attempt++) {
        try {
            [IO.File]::Replace($source, $target, [System.Management.Automation.Language.NullString]::Value)
            $replaced = $true
            break
        } catch {
            if ($attempt -eq 39) { throw }
            Start-Sleep -Milliseconds 500
        }
    }
    Restart-Editor
} catch {
    $failure = $_.Exception.Message
    if (Test-Path -LiteralPath (Join-Path $stage 'commit')) {
        if (!$replaced -and $editorProcess -and $editorProcess.HasExited) {
            try { Restart-Editor } catch {}
        }
        Add-Type -AssemblyName System.Windows.Forms
        [Windows.Forms.MessageBox]::Show($failure, 'Video Editor update failed') | Out-Null
    }
    exit 1
} finally {
    for ($attempt = 0; $attempt -lt 10; $attempt++) {
        try {
            Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction Stop
            break
        } catch {
            if ($attempt -eq 9) { throw }
            Start-Sleep -Milliseconds 200
        }
    }
}
