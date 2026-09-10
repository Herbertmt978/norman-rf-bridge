[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$configPath = Join-Path $repoRoot 'esphome\norman-rf-bridge.yaml'
$buildPath = 'D:\CodexBuild\norman-rf-bridge\esphome-stage1-monitor'
$env:ESPHOME_BUILD_PATH = $buildPath

. (Join-Path $PSScriptRoot 'common.ps1')
Assert-NormanRfEspHomeVersion
Protect-NormanRfBuildDirectory $buildPath

python -m esphome compile $configPath
if ($LASTEXITCODE -ne 0) { throw 'ESPHome Stage 1 compile failed.' }
