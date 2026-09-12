[CmdletBinding()]
param([switch]$AllowDirty)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'common.ps1')
Assert-NormanRfEspHomeVersion
$drive = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='D:'"
if ($drive.DriveType -ne 3 -or $drive.FreeSpace -lt 2GB) { throw 'A fixed D: drive with 2 GiB free is required.' }
$revision = (& git -C $repoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'No Git source revision.' }
$dirty = @(& git -C $repoRoot status --porcelain).Count -gt 0
if ($dirty -and -not $AllowDirty) { throw 'Dirty source: review it or explicitly use -AllowDirty for a development bundle.' }

function Get-SourceInventory {
    $paths = @(& git -C $repoRoot ls-files --cached --others --exclude-standard) | Sort-Object -Unique
    foreach ($relative in $paths) {
        if ($relative -notmatch '^(esphome/|protocol/|scripts/|docs/|README\.md$|LICENSE$|\.gitignore$)') { continue }
        if ($relative -match '^docs/(aegis|history)/|(^|/)captures/|\.(ci16|log)$|\.local\.json$') { continue }
        if ($relative -match '(^|/)(secrets[^/]*|__pycache__)(/|$)|\.(bin|elf|pyc)$') { continue }
        $source = Join-Path $repoRoot $relative
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { continue }
        [ordered]@{ file=$relative; bytes=(Get-Item -LiteralPath $source).Length; sha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() }
    }
}
$before = @(Get-SourceInventory)
& (Join-Path $PSScriptRoot 'test-protocol.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Protocol verification failed.' }
& (Join-Path $PSScriptRoot 'build-stage1.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Experimental firmware build failed.' }
$after = @(Get-SourceInventory)
if (($before | ConvertTo-Json -Depth 4 -Compress) -ne ($after | ConvertTo-Json -Depth 4 -Compress)) {
    throw 'Source changed during build; no package will be produced.'
}
$config = Get-Content -LiteralPath (Join-Path $repoRoot 'esphome/norman-rf-bridge.yaml') -Raw
$version = [regex]::Match($config, '(?m)^\s*project_version:\s*([^\s#]+)').Groups[1].Value
if ($version -notmatch '^\d+\.\d+\.\d+-experimental$') { throw 'This packager only creates labelled experimental artifacts.' }
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$destination = "D:\CodexBuild\norman-rf-bridge\experimental\$version-$stamp"
if (Test-Path -LiteralPath $destination) { throw 'Output already exists; refusing overwrite.' }
Protect-NormanRfBuildDirectory $destination
$built = 'D:\CodexBuild\norman-rf-bridge\esphome-stage1-monitor\norman-rf-bridge\.pioenvs\norman-rf-bridge'
foreach ($kind in 'factory', 'ota') {
    Copy-Item -LiteralPath (Join-Path $built "firmware.$kind.bin") -Destination (Join-Path $destination "firmware.$kind.bin")
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::Open((Join-Path $destination 'source.zip'), [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($entry in $after) {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, (Join-Path $repoRoot $entry.file), $entry.file) | Out-Null
    }
} finally { $zip.Dispose() }

$webManifest = [ordered]@{
    name='Norman RF Bridge - experimental'; version=$version; new_install_improv_wait_time=10
    builds=@(@{chipFamily='ESP32'; parts=@(@{path='firmware.factory.bin'; offset=0})})
}
$webManifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $destination 'web-install-manifest.json') -Encoding utf8
$artifacts = foreach ($name in 'firmware.factory.bin', 'firmware.ota.bin', 'source.zip', 'web-install-manifest.json') {
    $file = Join-Path $destination $name
    [ordered]@{file=$name; bytes=(Get-Item -LiteralPath $file).Length; sha256=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$manifest = [ordered]@{
    schema_version=1; project='herbertmt978.norman-rf-bridge'; version=$version
    source_revision=$revision; source_dirty=$dirty; source_files=$after; artifacts=@($artifacts)
    built_at_utc=[DateTime]::UtcNow.ToString('o'); esphome_version='2026.4.1'
    status='experimental-not-a-customer-release'; api_encrypted=$false; ota_authenticated=$false
    factory_commissioned=$false; maximum_panels=32; native_protocol=3; maximum_batch_targets=8
    learning_api_version=1; maximum_relay_profiles=32; profile_management='Home Assistant Reconfigure'
    repeat_gap_after_completion_ms=2000; maximum_extra_direct_bursts=2
    relay_path='15 -> 39 -> 59, learned commands only'
}
$manifest | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding utf8
Write-Output ([pscustomobject]@{Directory=$destination; Version=$version; SourceDirty=$dirty; Artifacts=4})
