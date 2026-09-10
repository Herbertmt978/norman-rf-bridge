[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$configPath = Join-Path $repoRoot 'esphome\stage0-recovery.yaml'
$buildPath = 'D:\CodexBuild\norman-rf-bridge\esphome'
$env:ESPHOME_BUILD_PATH = $buildPath

. (Join-Path $PSScriptRoot 'common.ps1')
Assert-NormanRfEspHomeVersion
Protect-NormanRfBuildDirectory $buildPath

python -m esphome compile $configPath
$compileExitCode = $LASTEXITCODE
if ($compileExitCode -ne 0) { throw 'ESPHome Stage 0 compile failed.' }
