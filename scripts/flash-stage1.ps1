[CmdletBinding()]
param(
    [string]$Port = 'COM3'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$configPath = Join-Path $repoRoot 'esphome\norman-rf-bridge.yaml'
$buildPath = 'D:\CodexBuild\norman-rf-bridge\esphome-stage1-monitor'
$env:ESPHOME_BUILD_PATH = $buildPath

. (Join-Path $PSScriptRoot 'common.ps1')
Assert-NormanRfEspHomeVersion
Protect-NormanRfBuildDirectory $buildPath

$availablePorts = [System.IO.Ports.SerialPort]::GetPortNames()
if ($availablePorts -notcontains $Port) {
    throw "Serial port $Port is not present."
}

$probeOutput = & python -m esptool --port $Port chip-id 2>&1
$probeExitCode = $LASTEXITCODE
$probeText = $probeOutput -join "`n"
if ($probeExitCode -ne 0 -or $probeText -notmatch 'ESP32-D0WD-V3') {
    throw "Refusing to flash: $Port was not confirmed as the expected ESP32-D0WD-V3."
}
Write-Host "Confirmed the expected ESP32-D0WD-V3 on $Port."

python -m esphome upload $configPath --device $Port
if ($LASTEXITCODE -ne 0) { throw "ESPHome Stage 1 upload to $Port failed." }
