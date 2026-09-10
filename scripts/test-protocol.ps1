[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$sourcePath = Join-Path $repoRoot 'protocol'
$buildPath = 'D:\CodexBuild\norman-rf-bridge\protocol'
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer vswhere.exe was not found.'
}

$cmake = & $vswhere -latest -products * -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' | Select-Object -First 1
$ctest = & $vswhere -latest -products * -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' | Select-Object -First 1
if (-not $cmake -or -not $ctest) {
    throw 'Visual Studio CMake/CTest tools were not found.'
}

New-Item -ItemType Directory -Path $buildPath -Force | Out-Null
& $cmake -S $sourcePath -B $buildPath -G 'Visual Studio 18 2026' -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& $cmake --build $buildPath --config Debug
if ($LASTEXITCODE -ne 0) { throw 'Protocol build failed.' }
& $ctest --test-dir $buildPath -C Debug --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Protocol tests failed.' }
