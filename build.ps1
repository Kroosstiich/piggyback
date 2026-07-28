#Requires -Version 5.1
<#
Builds the Piggyback SKSE plugin (C++, CommonLibSSE-NG) and its Papyrus API.

Handles the MSVC v143 environment, vcpkg, and the CMake and Ninja shipped with Visual Studio. The
first configure builds CommonLibSSE-NG and its dependencies from source, which takes a while (10 to
30 minutes); later builds are fast thanks to the vcpkg cache.

Paths specific to your machine are read from environment variables, so you never have to edit this
file:

  $env:VS_PATH           = "C:\Program Files\Microsoft Visual Studio\2022\Community"
  $env:VCPKG_ROOT        = "C:\dev\vcpkg"
  $env:SKYRIM_SE_PATH    = "D:\Steam\steamapps\common\Skyrim Special Edition"
  $env:MO2_INSTANCE_PATH = "C:\Users\<you>\AppData\Local\ModOrganizer\<your instance>"

Only SKYRIM_SE_PATH really matters, and only for the Papyrus compiler. Visual Studio and vcpkg are
auto-detected when the variables are not set.
#>
param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release"
)

# Continue, not Stop: cmake and vcpkg write informational messages to stderr, and in PowerShell 5.1 a
# native executable writing to stderr is treated as a fatal error under "Stop" (a false positive). We
# rely on the REAL exit code ($LASTEXITCODE) instead, checked explicitly after each critical step.
$ErrorActionPreference = "Continue"

$ProjectRoot = $PSScriptRoot
$ToolsetVer  = "14.44"  # v143. Pinned: see the build notes in DOCUMENTATION.md

function Fail($m) { Write-Host "FAILED: $m" -ForegroundColor Red; exit 1 }

# --- Visual Studio ------------------------------------------------------------------------------
# $env:VS_PATH wins; otherwise try the usual install locations, newest first.
$VsPath = $env:VS_PATH
if (-not $VsPath) {
    $candidates = @()
    foreach ($year in @("2026", "18", "2022", "17")) {
        foreach ($edition in @("Community", "Professional", "Enterprise", "BuildTools")) {
            $candidates += "C:\Program Files\Microsoft Visual Studio\$year\$edition"
        }
    }
    $VsPath = $candidates | Where-Object { Test-Path (Join-Path $_ "VC\Auxiliary\Build\vcvarsall.bat") } | Select-Object -First 1
}
if (-not $VsPath) {
    Fail "Visual Studio not found. Set `$env:VS_PATH to your installation, for example 'C:\Program Files\Microsoft Visual Studio\2022\Community'."
}

$VcVars   = Join-Path $VsPath "VC\Auxiliary\Build\vcvarsall.bat"
$CMakeBin = Join-Path $VsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$NinjaBin = Join-Path $VsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
foreach ($p in @($VcVars, $CMakeBin, $NinjaBin)) {
    if (-not (Test-Path $p)) { Fail "Not found: $p (is the 'Desktop development with C++' workload installed?)" }
}

# --- vcpkg --------------------------------------------------------------------------------------
# $env:VCPKG_ROOT wins; otherwise expect a sibling "vcpkg" folder next to the project.
$VcpkgRoot = $env:VCPKG_ROOT
if (-not $VcpkgRoot) { $VcpkgRoot = Join-Path (Split-Path $ProjectRoot -Parent) "vcpkg" }
if (-not (Test-Path $VcpkgRoot)) {
    Fail "vcpkg not found at '$VcpkgRoot'. Clone it and set `$env:VCPKG_ROOT."
}

$VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"
if (-not (Test-Path $VcpkgExe)) {
    Write-Host "=== Bootstrapping vcpkg ===" -ForegroundColor Cyan
    & (Join-Path $VcpkgRoot "bootstrap-vcpkg.bat") -disableMetrics
    if (-not (Test-Path $VcpkgExe)) { Fail "vcpkg bootstrap failed" }
}
$env:VCPKG_ROOT = $VcpkgRoot

# --- MSVC environment ---------------------------------------------------------------------------
# vcvarsall.bat calls vswhere.exe internally WITHOUT a full path, so the Installer folder has to be on
# PATH for it to be found. vcvars output goes to nul and "set" is captured into a temp file, so
# PowerShell does not treat native output as a fatal error.
Write-Host "=== MSVC $ToolsetVer environment (v143) ===" -ForegroundColor Cyan
$VsInstaller = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
$env:PATH = "$VsInstaller;$NinjaBin;$CMakeBin;$env:PATH"
$envDump = [System.IO.Path]::GetTempFileName()
cmd /c "call `"$VcVars`" x64 -vcvars_ver=$ToolsetVer > nul 2>&1 && set > `"$envDump`""
Get-Content $envDump | ForEach-Object {
    if ($_ -match "^(.*?)=(.*)$") { Set-Item -Path "Env:$($matches[1])" -Value $matches[2] }
}
Remove-Item $envDump -ErrorAction SilentlyContinue
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    Fail "MSVC environment not initialised (cl.exe not found after vcvarsall). Is the v143 toolset ($ToolsetVer) installed?"
}
Write-Host "OK: MSVC ready ($((Get-Command cl.exe).Source))"
$cmake = Join-Path $CMakeBin "cmake.exe"

# --- Build --------------------------------------------------------------------------------------
$preset    = "build-$($Config.ToLower())-msvc"
$binaryDir = Join-Path $ProjectRoot "build\$($Config.ToLower())-msvc"

Write-Host "=== CMake configure ($preset) ===" -ForegroundColor Cyan
& $cmake --preset $preset -S "$ProjectRoot"
if ($LASTEXITCODE -ne 0) { Fail "cmake configure (exit code $LASTEXITCODE)" }

Write-Host "=== Compiling ===" -ForegroundColor Cyan
& $cmake --build "$binaryDir"
if ($LASTEXITCODE -ne 0) { Fail "cmake build (exit code $LASTEXITCODE)" }

Write-Host "OK: DLL built. Set PIGGYBACK_OUTPUT_FOLDER to have it copied into a mod folder." -ForegroundColor Green

# --- Papyrus API --------------------------------------------------------------------------------
# Piggyback ships its own Piggyback.pex: it is what declares the native functions, so it has to travel
# with the DLL in the same mod. Consumer mods only IMPORT it at compile time, they do not redistribute
# it.
$GameDir = $env:SKYRIM_SE_PATH
if (-not $GameDir) { $GameDir = "C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition" }
$MO2Instance = $env:MO2_INSTANCE_PATH
if (-not $MO2Instance) { $MO2Instance = Join-Path $env:LOCALAPPDATA "ModOrganizer\Skyrim Special Edition" }

$PapyrusC   = Join-Path $GameDir "Papyrus Compiler\PapyrusCompiler.exe"
$FlagsFile  = Join-Path $GameDir "Data\Source\Scripts\TESV_Papyrus_Flags.flg"
$VanillaSrc = Join-Path $GameDir "Data\Scripts\Source"
$ScriptsSrc = Join-Path $ProjectRoot "Scripts\Source"
$PexOut     = Join-Path $MO2Instance "mods\Piggyback-dev\Scripts"

# MO2 sometimes redirects "Papyrus Compiler" into its virtual Overwrite\Root folder (seen after
# running the Creation Kit through MO2), which makes it "disappear" from the real game folder.
if (-not (Test-Path $PapyrusC)) {
    $overwriteSrc = Join-Path $MO2Instance "overwrite\Root\Papyrus Compiler"
    if (Test-Path $overwriteSrc) {
        Write-Host "Papyrus Compiler missing, restoring it from Overwrite..." -ForegroundColor Yellow
        $dst = Join-Path $GameDir "Papyrus Compiler"
        New-Item -ItemType Directory -Force -Path $dst | Out-Null
        Copy-Item -Path (Join-Path $overwriteSrc "*") -Destination $dst -Force
    }
}

if ((Test-Path $PapyrusC) -and (Test-Path $ScriptsSrc)) {
    Write-Host "=== Compiling Papyrus (API) ===" -ForegroundColor Cyan
    New-Item -ItemType Directory -Force -Path $PexOut | Out-Null
    & $PapyrusC $ScriptsSrc -all -output="$PexOut" -import="$VanillaSrc;$ScriptsSrc" -flags="$FlagsFile"
    if ($LASTEXITCODE -ne 0) { Fail "PapyrusCompiler (exit code $LASTEXITCODE)" }
    Write-Host "OK: Piggyback.pex written to $PexOut" -ForegroundColor Green
} else {
    Write-Host "Papyrus compilation skipped (compiler or sources not found). Set `$env:SKYRIM_SE_PATH to enable it." -ForegroundColor Yellow
}

Write-Host "`nDone." -ForegroundColor Green
