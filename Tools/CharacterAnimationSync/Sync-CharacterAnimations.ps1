param(
    [string]$SourceProject = 'R:\UE_WorkSpace\GameAnimationSample\GameAnimationSample.uproject',
    [string]$TargetProject = 'S:\UE_WorkSpace\SilverChoir\SilverChoir.uproject',
    [string]$EngineRoot = 'R:\EpicGame\UE_5.8',
    [string]$PythonExecutable = 'C:\Users\GENG\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe',
    [switch]$Preview
)
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent (Resolve-Path -LiteralPath $SourceProject).Path
$targetRoot = Split-Path -Parent (Resolve-Path -LiteralPath $TargetProject).Path
$running = Get-CimInstance Win32_Process -Filter "Name = 'UnrealEditor.exe' OR Name = 'UnrealEditor-Cmd.exe'" |
    Where-Object { $_.CommandLine -and ($_.CommandLine.Contains($sourceRoot) -or $_.CommandLine.Contains($targetRoot)) }
if ($running) { throw 'Save and close both project editors before syncing.' }
$workFolder = Join-Path $targetRoot 'Saved/CharacterAnimationSync'
[System.IO.Directory]::CreateDirectory($workFolder) | Out-Null
$manifestPath = Join-Path $workFolder ('manifest-' + [guid]::NewGuid().ToString('N') + '.json')
$exportScript = Join-Path $PSScriptRoot 'export_manifest.py'
$oldManifest = $env:CHARACTER_SYNC_MANIFEST
try {
    $env:CHARACTER_SYNC_MANIFEST = $manifestPath
    & (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') $SourceProject '-run=pythonscript' "-script=$exportScript" '-unattended' '-nop4' '-nosplash' '-nosound' '-NullRHI' "-abslog=$workFolder/Export.log" *> (Join-Path $workFolder 'Export.console.log')
    if (-not (Test-Path -LiteralPath $manifestPath)) { throw 'Export failed; see Saved/CharacterAnimationSync/Export.log.' }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.status -ne 'PASS') { throw 'Dependency export failed; inspect the manifest.' }
    $arguments = @((Join-Path $PSScriptRoot 'sync_files.py'), '--manifest', $manifestPath, '--target', $targetRoot)
    if (-not $Preview) { $arguments += '--apply' }
    & $PythonExecutable @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Sync failed; inspect Saved/CharacterAnimationSync/Report.json.' }
} finally {
    $env:CHARACTER_SYNC_MANIFEST = $oldManifest
}
