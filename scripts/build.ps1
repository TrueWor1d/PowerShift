param([switch]$Test)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$testOption = if ($Test) { 'ON' } else { 'OFF' }

cmake -S $root -B $build -A x64 "-DPOWERSHIFT_BUILD_TESTS=$testOption"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
if ($Test) {
    ctest --test-dir $build -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
}
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item (Join-Path $build 'Release/PowerShift.exe') $dist -Force
Copy-Item (Join-Path $build 'generated/使用说明.txt') $dist -Force
$version = [System.Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $dist 'PowerShift.exe')).ProductVersion
Write-Host "PowerShift ${version}: $dist"
