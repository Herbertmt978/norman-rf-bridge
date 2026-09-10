[CmdletBinding()]
param(
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$configPath = Join-Path $repoRoot 'esphome\stage0-recovery.yaml'
$buildRoot = 'D:\CodexBuild\norman-rf-bridge\esphome'
$firmwareRoot = Join-Path $buildRoot 'norman-rf-bridge\.pioenvs\norman-rf-bridge'
$releaseRoot = 'D:\CodexBuild\norman-rf-bridge\release'

. (Join-Path $PSScriptRoot 'common.ps1')
Assert-NormanRfEspHomeVersion

$gitRevision = (& git -C $repoRoot rev-parse HEAD 2>$null | Select-Object -First 1)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($gitRevision)) {
    throw 'Create a source commit before packaging a Stage 0 release.'
}

$gitStatus = @(& git -C $repoRoot status --porcelain)
if (-not $AllowDirty -and $gitStatus.Count -gt 0) {
    throw 'Refusing to package a dirty worktree. Commit the intended source or pass -AllowDirty for a development artifact.'
}

& (Join-Path $PSScriptRoot 'build-stage0.ps1')
if ($LASTEXITCODE -ne 0) {
    throw 'Stage 0 build failed before packaging.'
}

$configText = Get-Content -Raw -LiteralPath $configPath
$versionMatch = [regex]::Match($configText, '(?m)^\s*project_version:\s*([^\s#]+)')
if (-not $versionMatch.Success) {
    throw 'Unable to read project_version from the ESPHome configuration.'
}
$projectVersion = $versionMatch.Groups[1].Value
$releaseDirectory = Join-Path $releaseRoot $projectVersion
Protect-NormanRfBuildDirectory $releaseDirectory

$artifactDefinitions = @(
    [pscustomobject]@{
        Source = Join-Path $firmwareRoot 'firmware.factory.bin'
        Name   = "norman-rf-bridge-$projectVersion.factory.bin"
        Purpose = 'Initial USB/browser installation'
    },
    [pscustomobject]@{
        Source = Join-Path $firmwareRoot 'firmware.ota.bin'
        Name   = "norman-rf-bridge-$projectVersion.ota.bin"
        Purpose = 'ESPHome OTA update'
    }
)

$artifacts = [Collections.Generic.List[object]]::new()
foreach ($definition in $artifactDefinitions) {
    if (-not (Test-Path -LiteralPath $definition.Source -PathType Leaf)) {
        throw "Expected build artifact is missing: $($definition.Source)"
    }
    $destination = Join-Path $releaseDirectory $definition.Name
    Copy-Item -LiteralPath $definition.Source -Destination $destination -Force
    $item = Get-Item -LiteralPath $destination
    $hash = Get-FileHash -LiteralPath $destination -Algorithm SHA256
    $artifacts.Add([ordered]@{
            file    = $definition.Name
            purpose = $definition.Purpose
            bytes   = $item.Length
            sha256  = $hash.Hash.ToLowerInvariant()
        })
}

$manifest = [ordered]@{
    schema_version  = 1
    project         = 'herbertmt978.norman-rf-bridge'
    project_version = $projectVersion
    source_revision = $gitRevision.Trim()
    source_dirty    = ($gitStatus.Count -gt 0)
    esphome_version = '2026.4.1'
    built_at_utc    = [DateTime]::UtcNow.ToString('o')
    stage           = 0
    rf_capability   = 'absent'
    artifacts       = $artifacts
}

$manifestPath = Join-Path $releaseDirectory 'manifest.json'
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8

Write-Output ([pscustomobject]@{
        Version      = $projectVersion
        Source       = $gitRevision.Trim()
        ReleasePath  = $releaseDirectory
        ManifestPath = $manifestPath
    })
