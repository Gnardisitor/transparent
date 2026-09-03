# Builds the Windows installer: compiles with the 'windows' preset, stages a
# deploy tree (exe, Qt runtime via windeployqt, VC++ runtime, Vulkan loader,
# default models), and packs it with Inno Setup into
# build\dist\Transparent-<version>-x64-setup.exe.
#
# Usage:
#   powershell -File packaging\build-installer.ps1
#   powershell -File packaging\build-installer.ps1 -NoPackaging   # stage + smoke only

[CmdletBinding()]
param(
  [string]$QtDir = "C:\Qt\6.8.3\msvc2022_64",
  [string]$IsccPath = "",
  [switch]$NoPackaging
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "build"
$stageDir = Join-Path $buildDir "install-stage"
$distDir = Join-Path $buildDir "dist"

# Version from project() in CMakeLists.txt; drives the installer filename and
# Inno Setup's upgrade comparison.
$cmakeLists = Get-Content (Join-Path $repoRoot "CMakeLists.txt") -Raw
if ($cmakeLists -notmatch 'project\(\s*transparent\s+VERSION\s+([\d.]+)') {
  throw "Could not read VERSION from project() in CMakeLists.txt"
}
$version = $Matches[1]
Write-Host "Packaging Transparent $version"

# Configure only when there is no build tree yet, so a rebuild does not redo
# the model downloads.
if (-not (Test-Path (Join-Path $buildDir "CMakeCache.txt"))) {
  cmake --preset windows
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exe = Join-Path $buildDir "transparent.exe"
$vulkanDll = Join-Path $buildDir "vulkan-1.dll"
$modelsDir = Join-Path $buildDir "models"
foreach ($artifact in @($exe, $vulkanDll)) {
  if (-not (Test-Path $artifact)) { throw "Missing build artifact: $artifact" }
}

# Layout must match bundledModelsDefaultsDir() in src/main.cpp.
$stageBin = Join-Path $stageDir "bin"
$stageModels = Join-Path $stageDir "share\transparent\models"
if (Test-Path $stageDir) { Remove-Item -Recurse -Force $stageDir }
New-Item -ItemType Directory -Force -Path $stageBin, $stageModels | Out-Null

Copy-Item $exe -Destination $stageBin
Copy-Item $vulkanDll -Destination $stageBin

$windeployqt = Join-Path $QtDir "bin\windeployqt.exe"
if (-not (Test-Path $windeployqt)) {
  throw "windeployqt not found at $windeployqt (adjust -QtDir)"
}
& $windeployqt --release --compiler-runtime (Join-Path $stageBin "transparent.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }

# windeployqt skips the VC runtime when VCINSTALLDIR is not set (no VS dev
# shell), so locate the redist via vswhere and copy it.
if (-not (Test-Path (Join-Path $stageBin "vcruntime140.dll"))) {
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found - cannot locate the VC++ runtime" }
  $vsInstall = & $vswhere -latest -products * -property installationPath
  if (-not $vsInstall) { throw "No Visual Studio installation found by vswhere" }
  $redistRoot = Join-Path $vsInstall "VC\Redist\MSVC"
  $crtDir = Get-ChildItem $redistRoot -Directory |
    Where-Object { $_.Name -match '^\d' -and (Test-Path (Join-Path $_.FullName "x64")) } |
    Sort-Object Name -Descending |
    Select-Object -First 1
  if (-not $crtDir) { throw "No VC++ runtime redist found under $redistRoot" }
  $crt = Get-ChildItem (Join-Path $crtDir.FullName "x64") -Directory |
    Where-Object Name -like "Microsoft.VC*.CRT" |
    Select-Object -First 1
  if (-not $crt) { throw "No Microsoft.VC*.CRT directory under $($crtDir.FullName)\x64" }
  Write-Host "Deploying VC++ runtime from $($crt.FullName)"
  Copy-Item (Join-Path $crt.FullName "*.dll") -Destination $stageBin
}
if (-not (Test-Path (Join-Path $stageBin "vcruntime140.dll"))) {
  throw "vcruntime140.dll still missing from staged bin/"
}

Copy-Item (Join-Path $modelsDir "*.gguf") -Destination $stageModels
$stagedModels = (Get-ChildItem (Join-Path $stageModels "*.gguf")).Count
if ($stagedModels -lt 3) {
  throw "Expected 3 default models in $modelsDir, found $stagedModels (run a configure to fetch them)"
}

if ($NoPackaging) {
  Write-Host "Skipping Inno Setup (-NoPackaging). Staged tree: $stageDir"
  exit 0
}

# Find ISCC.exe: -IsccPath wins, then the registry (winget installs per-user,
# outside Program Files), then PATH.
if ($IsccPath -and (Test-Path $IsccPath)) {
  $iscc = (Resolve-Path $IsccPath).Path
} else {
  $uninstallKeys = @(
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
    'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
    'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*')
  $innoLocation = (Get-ItemProperty $uninstallKeys -ErrorAction SilentlyContinue |
    Where-Object { $_.DisplayName -like 'Inno Setup*' } |
    Select-Object -First 1).InstallLocation
  $candidates = @(
    (Join-Path $innoLocation "ISCC.exe"),
    (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
  )
  $iscc = $candidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
  if (-not $iscc) {
    throw "ISCC.exe not found - install Inno Setup 6 (winget install -e --id JRSoftware.InnoSetup) or pass -IsccPath"
  }
}

New-Item -ItemType Directory -Force -Path $distDir | Out-Null
& $iscc "/DAppVersion=$version" "/DStageDir=$stageDir" (Join-Path $PSScriptRoot "transparent.iss")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$setupExe = Join-Path $distDir "Transparent-$version-x64-setup.exe"
if (-not (Test-Path $setupExe)) { throw "Inno Setup reported success but $setupExe is missing" }
Write-Host "Installer created: $setupExe"
