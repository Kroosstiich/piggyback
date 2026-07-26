#Requires -Version 5.1
<#
Build du plugin SKSE "Piggyback" (rig moteur C++, CommonLibSSE-NG). Equivalent de build.ps1 mais pour
le DLL. Gere l'environnement MSVC v143 (14.44, evite les frictions du v145 tout neuf de VS 2026), vcpkg,
CMake et Ninja fournis par Visual Studio. La 1re configuration compile CommonLibSSE-NG + deps depuis les
sources (long, 10-30 min) ; les suivantes sont rapides (cache vcpkg).
#>
param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release"
)

# Continue (pas Stop) : cmake/vcpkg ecrivent des messages informatifs sur stderr, et en PowerShell 5.1
# un exe natif qui ecrit sur stderr est traite comme une erreur fatale sous "Stop" (faux positif). On se
# fie donc au VRAI code de sortie ($LASTEXITCODE), verifie explicitement apres chaque etape critique.
$ErrorActionPreference = "Continue"
# Piggyback est desormais un projet AUTONOME (mymods\Piggyback), plus un sous-dossier de VelynTheNetch.
# vcpkg est partage entre les composants Piggy* -> il vit un cran au-dessus (mymods\vcpkg).
$ProjectRoot = $PSScriptRoot
$PluginDir   = $ProjectRoot
$VcpkgRoot   = Join-Path (Split-Path $ProjectRoot -Parent) "vcpkg"
$VsPath      = "C:\Program Files\Microsoft Visual Studio\18\Community"
$VcVars      = Join-Path $VsPath "VC\Auxiliary\Build\vcvarsall.bat"
$CMakeBin    = Join-Path $VsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$NinjaBin    = Join-Path $VsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
$ToolsetVer  = "14.44"  # v143 installe cote a cote (voir docs/02-plugin-rig-moteur.md)

function Fail($m) { Write-Host "ECHEC: $m" -ForegroundColor Red; exit 1 }
foreach ($p in @($VcVars, $CMakeBin, $NinjaBin)) { if (-not (Test-Path $p)) { Fail "Introuvable: $p" } }

# Bootstrap vcpkg si besoin
$VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"
if (-not (Test-Path $VcpkgExe)) {
    Write-Host "=== Bootstrap vcpkg ===" -ForegroundColor Cyan
    & (Join-Path $VcpkgRoot "bootstrap-vcpkg.bat") -disableMetrics
    if (-not (Test-Path $VcpkgExe)) { Fail "bootstrap vcpkg a echoue" }
}
$env:VCPKG_ROOT = $VcpkgRoot

# Importer l'environnement MSVC v143 (vcvarsall) dans cette session.
# vcvarsall.bat appelle vswhere.exe en interne SANS chemin complet -> on ajoute le dossier Installer au
# PATH pour qu'il le trouve. On redirige la sortie de vcvars vers nul et on capture "set" dans un fichier
# temp (evite que PowerShell traite la sortie native comme une erreur fatale).
Write-Host "=== Environnement MSVC $ToolsetVer (v143) ===" -ForegroundColor Cyan
$VsInstaller = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
$env:PATH = "$VsInstaller;$NinjaBin;$CMakeBin;$env:PATH"
$envDump = [System.IO.Path]::GetTempFileName()
cmd /c "call `"$VcVars`" x64 -vcvars_ver=$ToolsetVer > nul 2>&1 && set > `"$envDump`""
Get-Content $envDump | ForEach-Object {
    if ($_ -match "^(.*?)=(.*)$") { Set-Item -Path "Env:$($matches[1])" -Value $matches[2] }
}
Remove-Item $envDump -ErrorAction SilentlyContinue
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { Fail "environnement MSVC non initialise (cl.exe introuvable apres vcvarsall)" }
Write-Host "OK : MSVC pret ($((Get-Command cl.exe).Source))"
$cmake = Join-Path $CMakeBin "cmake.exe"

$preset   = "build-$($Config.ToLower())-msvc"
$binaryDir = Join-Path $PluginDir "build\$($Config.ToLower())-msvc"

Write-Host "=== Configuration CMake ($preset) ===" -ForegroundColor Cyan
& $cmake --preset $preset -S "$PluginDir"
if ($LASTEXITCODE -ne 0) { Fail "cmake configure (code $LASTEXITCODE)" }

Write-Host "=== Compilation ===" -ForegroundColor Cyan
& $cmake --build "$binaryDir"
if ($LASTEXITCODE -ne 0) { Fail "cmake build (code $LASTEXITCODE)" }

Write-Host "OK : DLL compile et deploye (voir CMakeLists : mods\Piggyback-dev)." -ForegroundColor Green

# --- Papyrus : l'API du composant ---------------------------------------------------------------
# Piggyback livre son propre Piggyback.pex : c'est lui qui declare les fonctions natives, il doit donc
# accompagner le DLL dans le meme mod. Les mods consommateurs (Velyn...) se contentent de l'IMPORTER a
# la compilation, sans le redistribuer.
# Chemins specifiques a votre machine. Deux facons de les definir, sans modifier ce fichier :
#   $env:SKYRIM_SE_PATH   = "D:\Steam\steamapps\common\Skyrim Special Edition"
#   $env:MO2_INSTANCE_PATH = "C:\Users\<vous>\AppData\Local\ModOrganizer\<votre instance>"
# Sinon, les valeurs par defaut ci-dessous sont utilisees.
$GameDir = $env:SKYRIM_SE_PATH
if (-not $GameDir) { $GameDir = "C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition" }
$MO2Instance = $env:MO2_INSTANCE_PATH
if (-not $MO2Instance) { $MO2Instance = Join-Path $env:LOCALAPPDATA "ModOrganizer\Skyrim Special Edition" }
$PapyrusC    = Join-Path $GameDir "Papyrus Compiler\PapyrusCompiler.exe"
$FlagsFile   = Join-Path $GameDir "Data\Source\Scripts\TESV_Papyrus_Flags.flg"
$VanillaSrc  = Join-Path $GameDir "Data\Scripts\Source"
$ScriptsSrc  = Join-Path $ProjectRoot "Scripts\Source"
$PexOut      = Join-Path $MO2Instance "mods\Piggyback-dev\Scripts"

# MO2 redirige parfois "Papyrus Compiler" vers son dossier virtuel Overwrite\Root (observe apres des
# sessions Creation Kit lancees via MO2), ce qui le fait "disparaitre" du vrai dossier du jeu.
if (-not (Test-Path $PapyrusC)) {
    $overwriteSrc = Join-Path $MO2Instance "overwrite\Root\Papyrus Compiler"
    if (Test-Path $overwriteSrc) {
        Write-Host "Papyrus Compiler manquant, restauration depuis Overwrite..." -ForegroundColor Yellow
        $dst = Join-Path $GameDir "Papyrus Compiler"
        New-Item -ItemType Directory -Force -Path $dst | Out-Null
        Copy-Item -Path (Join-Path $overwriteSrc "*") -Destination $dst -Force
    }
}

if ((Test-Path $PapyrusC) -and (Test-Path $ScriptsSrc)) {
    Write-Host "=== Compilation Papyrus (API) ===" -ForegroundColor Cyan
    New-Item -ItemType Directory -Force -Path $PexOut | Out-Null
    & $PapyrusC $ScriptsSrc -all -output="$PexOut" -import="$VanillaSrc;$ScriptsSrc" -flags="$FlagsFile"
    if ($LASTEXITCODE -ne 0) { Fail "PapyrusCompiler (code $LASTEXITCODE)" }
    Write-Host "OK : Piggyback.pex deploye dans $PexOut" -ForegroundColor Green
} else {
    Write-Host "Compilation Papyrus ignoree (compilateur ou sources introuvables)." -ForegroundColor Yellow
}

Write-Host "`nTermine." -ForegroundColor Green
