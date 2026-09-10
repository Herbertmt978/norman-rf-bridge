[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9.-]+$')]
    [string]$Device,

    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$configPath = Join-Path $repoRoot 'esphome\stage0-recovery.yaml'
$buildPath = 'D:\CodexBuild\norman-rf-bridge\esphome'
$firmwarePath = Join-Path $buildPath 'norman-rf-bridge\.pioenvs\norman-rf-bridge\firmware.bin'
$env:ESPHOME_BUILD_PATH = $buildPath

. (Join-Path $PSScriptRoot 'common.ps1')
Assert-NormanRfEspHomeVersion
Protect-NormanRfBuildDirectory $buildPath

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-stage0.ps1')
    if ($LASTEXITCODE -ne 0) {
        throw 'Stage 0 build failed before OTA.'
    }
}

if (-not (Test-Path -LiteralPath $firmwarePath -PathType Leaf)) {
    throw 'The Stage 0 OTA image is missing. Run scripts/build-stage0.ps1 first.'
}

python -m esphome upload $configPath --device $Device
if ($LASTEXITCODE -ne 0) {
    throw "ESPHome OTA upload to $Device failed."
}
