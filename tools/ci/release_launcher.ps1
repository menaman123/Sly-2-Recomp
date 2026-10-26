# Builds, tests and packages the Windows (x64) release:
#
#   <product>-<tag>-windows-x64.zip
#   └── <product>-windows-x64/
#       ├── launcher/   <product>-launcher.exe, the Qt DLLs (windeployqt, dynamic, LGPL), the C++ runtime,
#       │               titles/title.json, BUILD-ID, LICENSE, THIRD_PARTY_NOTICES.md, licenses/
#       └── game/       with -Game DIR: the game staged by engine\tools\ci\release_dist.ps1, nativeee.exe renamed to
#                       the manifest's binary. The launcher copies it into the player's game folder; the game reads
#                       its data from the player's own disc.
#
# Without -Game the zip carries the launcher only. The asset scan below keeps disc bytes out of the zip.
param(
  [string]$Qt = $env:QT_ROOT_DIR,
  [string]$BuildDir = "build-launcher",
  [string]$DistDir = "dist",
  [string]$Engine = "engine",
  [string]$Game = "",
  [switch]$SkipBuild,
  [string]$Tag = ""
)
$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $Root
function Step($t) { Write-Host ""; Write-Host "=== $t" }
if (-not $Qt) { throw "release_launcher: -Qt PREFIX (or QT_ROOT_DIR) is required" }
if (-not (Test-Path "$Engine\app\launcher\CMakeLists.txt")) { throw "release_launcher: the engine at $Engine has no app\launcher" }
$Manifest = Get-Content "launcher\title.json" -Raw | ConvertFrom-Json
$Product = $Manifest.product; $Name = $Manifest.name; $TitleId = $Manifest.id
$Platform = "windows-x64"
if (-not $Manifest.targets.PSObject.Properties.Name.Contains($Platform)) { throw "release_launcher: $Platform is not one of the manifest's targets" }
if (-not $Tag) { $Tag = (git describe --tags --always 2>$null); if (-not $Tag) { $Tag = (git rev-parse --short HEAD) } }
$Asset = $Manifest.release.asset_pattern.Replace('${product}', $Product).Replace('${tag}', $Tag).Replace('${platform}', $Platform)
$Bundle = $Manifest.release.bundle_folder.Replace('${product}', $Product).Replace('${platform}', $Platform)
Write-Host "product $Product ($TitleId) · platform $Platform · tag $Tag · asset $Asset"
if ($Game -and -not (Test-Path -LiteralPath (Join-Path $Game "nativeee.exe"))) { throw "release_launcher: -Game $Game has no nativeee.exe (stage it with engine\tools\ci\release_dist.ps1)" }

if (-not $SkipBuild) {
  Step "build the launcher ($Engine\app\launcher -> $BuildDir)"
  cmake -S "$Engine\app\launcher" -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$Qt" "-DPS2R_LAUNCHER_VERSION=$Tag"
  if ($LASTEXITCODE) { throw "configure failed" }
  cmake --build $BuildDir
  if ($LASTEXITCODE) { throw "build failed" }
  Step "the launcher's tests"
  Push-Location $BuildDir; $env:QT_QPA_PLATFORM = "offscreen"; ctest --output-on-failure; $rc = $LASTEXITCODE; Pop-Location
  if ($rc) { throw "tests failed" }
}
$Dist = Join-Path $DistDir $Bundle
Step "stage $Dist"
if (Test-Path $Dist) { Remove-Item -Recurse -Force $Dist }
$Res = Join-Path $Dist "launcher"
New-Item -ItemType Directory -Force -Path "$Res\titles", "$Res\licenses" | Out-Null
Copy-Item "$BuildDir\ps2r_launcher.exe" "$Res\$Product-launcher.exe"
Copy-Item "launcher\title.json" "$Res\titles\title.json"
$QtVersion = (& "$Qt\bin\qmake.exe" -query QT_VERSION)
@("$Product launcher $Tag ($Platform)", "title   $(git rev-parse HEAD)", "engine  $(git -C $Engine rev-parse HEAD)", "qt      $QtVersion", "built   $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))") | Set-Content "$Res\BUILD-ID"
Get-Content "$Res\BUILD-ID"
if (Test-Path "LICENSE") { Copy-Item "LICENSE" "$Res\LICENSE" }
foreach ($f in @("LICENSE.LGPL3", "LICENSE.GPL3-EXCEPT", "LICENSE.GPL3", "LICENSE.FDL")) { if (Test-Path "$Qt\..\..\Licenses\$f") { Copy-Item "$Qt\..\..\Licenses\$f" "$Res\licenses\Qt-$f" } }
if (-not (Test-Path "$Res\licenses\Qt-LICENSE.LGPL3") -and (Test-Path "$Engine\tools\dist\licenses\Qt-LICENSE.LGPL3.txt")) { Copy-Item "$Engine\tools\dist\licenses\Qt-LICENSE.LGPL3.txt" "$Res\licenses\Qt-LICENSE.LGPL3" }
@("# $Name -- third-party notices -- $Product launcher $Tag ($Platform)", "",
  "This application is the NativeEE engine's launcher (engine/app/launcher, MIT) with this title's description. It",
  "contains no game data. Qt $QtVersion is linked dynamically under the LGPL v3 (licenses/Qt-LICENSE.LGPL3) and may be",
  "replaced by another build of the same major version. The game in game/ carries its own notices.") | Set-Content "$Res\THIRD_PARTY_NOTICES.md"

Step "windeployqt (the Qt DLLs and plugins beside the executable, dynamic)"
& "$Qt\bin\windeployqt.exe" --release --no-translations --no-opengl-sw "$Res\$Product-launcher.exe"
if ($LASTEXITCODE) { throw "windeployqt failed" }
# The MSVC C++ runtime beside the launcher (app-local deployment, which Microsoft supports): windeployqt only adds
# vc_redist.x64.exe, and without the runtime installed the launcher would not start.
$crtDir = if ($env:VCToolsRedistDir) { Get-ChildItem -Path (Join-Path $env:VCToolsRedistDir "x64") -Directory -Filter "Microsoft.VC*.CRT" -ErrorAction SilentlyContinue | Select-Object -First 1 }
if ($crtDir) {
  Get-ChildItem -Path $crtDir.FullName -File | Where-Object { $_.Name -match '^(msvcp140.*|vcruntime140.*)\.dll$' } | Copy-Item -Destination $Res
  Write-Host "C++ runtime from $($crtDir.FullName)"
} else { Write-Host "warning: no VCToolsRedistDir CRT folder -- the launcher relies on an installed Visual C++ Redistributable" }

if ($Game) {
  Step "game\ (from $Game)"
  $GameDst = Join-Path $Dist "game"
  Copy-Item -Recurse -LiteralPath $Game -Destination $GameDst
  $Binary = $Manifest.targets.$Platform.binary
  Rename-Item -LiteralPath (Join-Path $GameDst "nativeee.exe") -NewName $Binary
  Get-ChildItem $GameDst | Select-Object Name, Length | Format-Table -AutoSize | Out-String | Write-Host
}

Step "no game-derived file in the staged folder"
if (Test-Path "$Engine\tools\ci\asset_scan.py") { python -P "$Engine\tools\ci\asset_scan.py" $Dist; if ($LASTEXITCODE) { throw "asset scan found something" } }

Step "$DistDir\$Asset"
$Zip = Join-Path (Resolve-Path $DistDir) $Asset
if (Test-Path $Zip) { Remove-Item $Zip }
Compress-Archive -Path $Dist -DestinationPath $Zip
"$((Get-FileHash $Zip -Algorithm SHA256).Hash.ToLower())  $Asset" | Set-Content "$Zip.sha256"
Get-Item $Zip | Select-Object Name, Length
Get-Content "$Zip.sha256"
