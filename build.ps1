# build.ps1 -- configure and build NativeEE (the engine submodule) with the Sly 2 title module on Windows.
# The Windows counterpart of build.sh (PowerShell 5.1+ / 7).
#
#   .\build.ps1 -Disc "X:\path\Sly 2 - Band of Thieves.iso"   your own disc image (NTSC-U, SCUS_973.16):
#                                       the ELF is extracted (SHA-256 gated) and recompiled
#   .\build.ps1 -Elf X:\path\SCUS_973.16                      the loose ELF instead
#   .\build.ps1 -NoDisc                 no game file: the engine libraries, the tools, the title's TitleProfile
#                                       library and the component tests only (what CI builds).
#                                       NATIVEEE_DISC / NATIVEEE_TITLE_ELF in the environment are ignored.
#   -Preset NAME        an ENGINE configure preset (engine\CMakePresets.json; default windows-release:
#                       clang-cl + Ninja, SDL2 + FFmpeg from the vcpkg manifest -- VCPKG_ROOT must point at a
#                       vcpkg checkout; the first configure builds them, tens of minutes). Its binaryDir is
#                       overridden by -BuildDir.
#   -BuildDir DIR       where to build (default: $env:SLY2_BUILD_DIR, else the sibling <checkout name>-build\;
#                       MUST be outside the repository: it receives the extracted ELF and the recompiled code)
#   -Type TYPE          CMAKE_BUILD_TYPE (default Release)
#   -Targets a,b,...    build target(s) (default: nativeee)
#   -Tests              build the whole tree (`all`, ninja -k 0; the targets in .github\ci\known_broken_targets.txt
#                       may fail, anything else is an error) and run ctest -E <.github\ci\ctest_exclusions.txt>
#   -ConfigureOnly      stop after configure
#   -Jobs N             parallel jobs
#
# Prerequisites (the engine's README, "Developers"): Visual Studio 2022 Build Tools with the "C++ Clang
# tools for Windows" component (clang-cl, Ninja) -- run from a "Developer PowerShell for VS 2022" or let
# the script import the environment (vswhere / VsDevCmd.bat); CMake 3.21+, Python 3.8+ on PATH; vcpkg.
# Acceptance shape (the engine's rule): 0 FAILED, 0 `warning:`, 0 `lld-link: warning` in the build log.
[CmdletBinding()]
param(
    [string]$Disc = "",
    [string]$Elf = "",
    [switch]$NoDisc,
    [string]$Preset = "windows-release",
    [string]$BuildDir = "",
    [string]$Type = "Release",
    [string[]]$Targets = @(),
    [switch]$Tests,
    [switch]$ConfigureOnly,
    [int]$Jobs = 0,
    [switch]$Help
)
$ErrorActionPreference = "Stop"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Engine = Join-Path $Here "engine"
$Games = Join-Path $Here "games"
$CiLists = Join-Path $Here ".github\ci\ci_lists.py"
if ($Help) {
    Get-Content $MyInvocation.MyCommand.Path | Select-Object -Skip 1 -First 30 | ForEach-Object { $_ -replace '^# ?', '' }
    exit 0
}
if (-not (Test-Path (Join-Path $Engine "CMakeLists.txt"))) {
    throw "build.ps1: engine submodule missing -- run: git submodule update --init"
}

# A native command whose stderr is merged (2>&1) terminates a Windows PowerShell 5.1 script under
# $ErrorActionPreference = "Stop" on its FIRST stderr line (a compiler warning, ninja's notes). Run such
# commands with the preference relaxed and judge them by $LASTEXITCODE (the engine's build.ps1 does the same).
function Invoke-Native([scriptblock]$Block) {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $global:LASTEXITCODE = 0
    try { & $Block | Out-Host } finally { $ErrorActionPreference = $saved }
    return $global:LASTEXITCODE
}

# --- the MSVC developer environment (clang-cl, Ninja, the Windows SDK on PATH) -------------------
if (-not (Get-Command clang-cl -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $devcmd = Join-Path $vs "Common7\Tools\VsDevCmd.bat"
        if (Test-Path $devcmd) {
            Write-Host "build.ps1: importing the VS developer environment ($devcmd)"
            cmd /c "`"$devcmd`" -arch=x64 -host_arch=x64 >nul && set" | ForEach-Object {
                if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process") }
            }
        }
        $llvm = Join-Path $vs "VC\Tools\Llvm\x64\bin"
        if ($vs -and (Test-Path (Join-Path $llvm "clang-cl.exe")) -and -not (Get-Command clang-cl -ErrorAction SilentlyContinue)) {
            $env:PATH = "$llvm;$env:PATH"
        }
    }
}
foreach ($tool in @("cmake", "ninja", "python")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "build.ps1: '$tool' not on PATH (see the prerequisites at the top of this script)" }
}
if ($Preset -like "windows-*") {
    if (-not (Get-Command clang-cl -ErrorAction SilentlyContinue) -and $Preset -ne "windows-msvc-dev") { throw "build.ps1: clang-cl not on PATH" }
    if (-not $env:VCPKG_ROOT -or -not (Test-Path (Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"))) {
        throw "build.ps1: VCPKG_ROOT must point at a vcpkg checkout (git clone https://github.com/microsoft/vcpkg; .\bootstrap-vcpkg.bat)"
    }
}

# --- the title input ------------------------------------------------------------------------------
if ($NoDisc) {
    if ($Disc -or $Elf) { throw "build.ps1: -NoDisc excludes -Disc / -Elf" }
} else {
    if (-not $Disc) { $Disc = $env:NATIVEEE_DISC }
    if (-not $Elf) { $Elf = $env:NATIVEEE_TITLE_ELF }
}
foreach ($pair in @(@("Disc", $Disc), @("Elf", $Elf))) {
    $name, $val = $pair
    if ($val -and -not (Test-Path -LiteralPath $val -PathType Leaf)) { throw "build.ps1: -$name ${val}: no such file" }
}

# --- the build directory: outside the repository ------------------------------------------------
if (-not $BuildDir) {
    if ($env:SLY2_BUILD_DIR) { $BuildDir = $env:SLY2_BUILD_DIR }
    else { $BuildDir = Join-Path (Split-Path -Parent $Here) ((Split-Path -Leaf $Here) + "-build") }
}
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
$RepoReal = (Resolve-Path -LiteralPath $Here).Path
if ($BuildDir.TrimEnd('\').StartsWith($RepoReal.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "build.ps1: the build dir must be outside the repository (it holds game-derived files): $BuildDir"
}

# --- configure: the engine is the SOURCE dir, this repository's games\ the title directory ---------
# PS2R_SIMD=ON selects the engine's NEON lane kernels on AArch64 only; on x86-64 the scalar twins.
$cmakeArgs = @("-S", $Engine, "-B", $BuildDir)
if ($Preset -ne "none") { $cmakeArgs += @("--preset", $Preset) } else { $cmakeArgs += @("-G", "Ninja") }
$cmakeArgs += @("-DCMAKE_BUILD_TYPE=$Type", "-DPS2R_DIAGNOSTICS=OFF", "-DPS2R_SIMD=ON", "-DPS2R_GAMES_DIR=$Games")
if ($Disc) { $cmakeArgs += @("-DNATIVEEE_DISC=$((Resolve-Path -LiteralPath $Disc).Path)", "-DNATIVEEE_TITLE_ELF=") }
if ($Elf) { $cmakeArgs += "-DNATIVEEE_TITLE_ELF=$((Resolve-Path -LiteralPath $Elf).Path)" }
if (-not $Disc -and -not $Elf) { $cmakeArgs += @("-DNATIVEEE_DISC=", "-DNATIVEEE_TITLE_ELF=") }   # a cache from a disc run must not leak in
Write-Host "build.ps1: cmake $($cmakeArgs -join ' ')"
$rc = Invoke-Native { cmake @cmakeArgs 2>&1 | ForEach-Object { "$_" } }
if ($rc -ne 0) { throw "build.ps1: configure failed" }
if ($ConfigureOnly) { exit 0 }

# --- build ------------------------------------------------------------------------------------------
$Log = Join-Path $BuildDir "build.ps1.log"
if ($Tests) {
    $buildArgs = @("--build", $BuildDir)
    if ($Jobs -gt 0) { $buildArgs += @("-j", "$Jobs") }
    $buildArgs += @("--", "-k", "0")
    Write-Host "build.ps1: cmake $($buildArgs -join ' ')"
    $rc = Invoke-Native { cmake @buildArgs 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $Log }
    $arc = Invoke-Native { python $CiLists audit-build $Log 2>&1 | ForEach-Object { "$_" } }
    if ($arc -ne 0) { throw "build.ps1: BUILD FAILED (log: $Log)" }
    if ($rc -ne 0) { Write-Host "build.ps1: build exit $rc -- only known-broken targets failed (see .github\ci\known_broken_targets.txt)" }
} else {
    if ($Targets.Count -eq 0) { $Targets = @("nativeee") }
    $buildArgs = @("--build", $BuildDir, "--target") + $Targets
    if ($Jobs -gt 0) { $buildArgs += @("-j", "$Jobs") }
    Write-Host "build.ps1: cmake $($buildArgs -join ' ')"
    $rc = Invoke-Native { cmake @buildArgs 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $Log }
    if ($rc -ne 0) { throw "build.ps1: BUILD FAILED (log: $Log)" }
}
$nWarn = (Select-String -Path $Log -Pattern "warning:" -SimpleMatch | Measure-Object).Count
$nLdWarn = (Select-String -Path $Log -Pattern "lld-link: warning" -SimpleMatch | Measure-Object).Count
Write-Host "build.ps1: build OK -- warning: $nWarn, lld-link: warning $nLdWarn"
if ($nWarn -ne 0 -or $nLdWarn -ne 0) { throw "build.ps1: REFUSING -- the build log has warnings (log: $Log)" }

# --- tests -------------------------------------------------------------------------------------------
if ($Tests) {
    $exclude = (& python $CiLists exclude-regex | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $exclude) { throw "build.ps1: ci_lists.py exclude-regex failed" }
    $ctestArgs = @("--test-dir", $BuildDir, "-E", $exclude, "--timeout", "900", "--output-on-failure")
    if ($Jobs -gt 0) { $ctestArgs += @("-j", "$Jobs") }
    Write-Host "build.ps1: ctest $($ctestArgs -join ' ')"
    $rc = Invoke-Native { ctest @ctestArgs 2>&1 | ForEach-Object { "$_" } }
    if ($rc -ne 0) { throw "build.ps1: TESTS FAILED (ctest --test-dir $BuildDir --rerun-failed --output-on-failure)" }
}
$exe = Join-Path $BuildDir "app\host\nativeee.exe"
if (Test-Path $exe) { Write-Host "build.ps1: binary at $exe" }
exit 0
