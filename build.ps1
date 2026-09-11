#Requires -Version 5.1
<#
Builds Piggyback without changing the game or a mod manager installation.
Set VCPKG_ROOT and optionally VS_PATH. Dependencies are pinned in CMake and vcpkg.
The Papyrus API is unchanged; use the existing Piggyback.pex when packaging.
#>
param(
    [ValidateSet("Release", "Debug")][string]$Config = "Release",
    [string]$OutputFolder = "",
    [ValidateRange(1, 32)][int]$Jobs = 4
)
$ErrorActionPreference = "Stop"
$ProjectRoot = $PSScriptRoot
$VsPath = $env:VS_PATH
if (-not $VsPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $VsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $VsPath) { throw "Visual Studio C++ tools not found; set VS_PATH." }
$vcvars = Join-Path $VsPath "VC\Auxiliary\Build\vcvarsall.bat"
$cmake = Join-Path $VsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninjaDir = Join-Path $VsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
$installerDir = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer"
$env:PATH = "$installerDir;$ninjaDir;$env:PATH"
$toolset = "14.44"
# Import the compiler environment without writing a file containing environment secrets.
$compilerEnv = & cmd.exe /d /s /c "call `"$vcvars`" x64 -vcvars_ver=$toolset >nul && set"
if ($LASTEXITCODE -ne 0) { throw "vcvarsall failed." }
foreach ($line in $compilerEnv) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process") }
}
if (-not $env:VCPKG_ROOT) { throw "Set VCPKG_ROOT to a bootstrapped vcpkg checkout." }
$env:VCPKG_MAX_CONCURRENCY = "$Jobs"
$binaryDir = Join-Path $ProjectRoot "build\skyrim-1.7-$($Config.ToLower())"
& $cmake -S $ProjectRoot -B $binaryDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" "-DVCPKG_TARGET_TRIPLET=x64-windows-skse" "-DVCPKG_HOST_TRIPLET=x64-windows-skse" "-DVCPKG_OVERLAY_TRIPLETS=$ProjectRoot/cmake" "-DPIGGYBACK_OUTPUT_FOLDER=$OutputFolder"
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed ($LASTEXITCODE)." }
& $cmake --build $binaryDir --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw "Compilation failed ($LASTEXITCODE)." }
Write-Host "Built: $binaryDir\Piggyback.dll"
