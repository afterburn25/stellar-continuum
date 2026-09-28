param(
    [ValidateSet('development','qa','release')][string]$Profile = 'release',
    [string]$BuildDirectory = 'build-native/preview',
    [string]$OutputDirectory = 'work/cooker/release',
    [string]$ArchivePath = '',
    [string]$PreviousReport = '',
    [switch]$VerifyLaunch,
    [switch]$SkipBuild,
    [switch]$SkipCook
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
# Windows PowerShell 5.1 compatibility helpers (see build-release-installer.ps1).
function Get-RelativePathCompat([string]$Base,[string]$Full){
    return ($Full.Substring($Base.Length) -replace '^[\\/]+','')
}
function Write-Utf8NoBomFile([string]$Path,[string]$Text){
    $Text=$Text -replace "`r`n","`n"
    $Text=$Text -replace "`n","`r`n"
    [IO.File]::WriteAllText($Path,$Text+"`r`n")
}
$build = [IO.Path]::GetFullPath($(if([IO.Path]::IsPathRooted($BuildDirectory)){$BuildDirectory}else{Join-Path $repo $BuildDirectory}))
$stage = [IO.Path]::GetFullPath($(if([IO.Path]::IsPathRooted($OutputDirectory)){$OutputDirectory}else{Join-Path $repo $OutputDirectory}))
if ($stage -eq $repo -or $stage -eq $build) { throw 'Choose a separate package output directory.' }
$cmake = 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
if (-not $SkipBuild) {
    # Native build requires the normal VS developer environment. Keep compile,
    # asset cooking and validation separate so failures stop publication.
    & $cmake --build $build --target stellar-continuum-native StellarCooker --parallel 4
    if ($LASTEXITCODE) { throw 'Native build failed. Run from a VS developer shell.' }
}
$cooker = Join-Path $build 'cooker-tools/StellarCooker.exe'
$cookReport = Join-Path $repo "work/cooker/$Profile-report.json"
if (-not $SkipCook) {
    & $cooker --root $repo --profile $Profile --output $stage --cache (Join-Path $repo 'work/cooker/cache') --report $cookReport --threads 4 --validate
    if ($LASTEXITCODE) { throw 'Asset cook failed.' }
}
$validation = Join-Path $repo "work/cooker/$Profile-validation.json"
& $cooker --validate-only --output $stage --report $validation
if ($LASTEXITCODE) { throw 'Cooked content validation failed.' }
$activePackages = (Get-Content -LiteralPath $validation -Raw | ConvertFrom-Json).packages
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $stage 'Content') -File) {
    if ($file.Name -ne 'runtime.stmanifest' -and $file.Name -notin $activePackages) {
        # Preserve earlier package generations outside the shipping folder.
        $archiveRoot = Join-Path $repo 'work/cooker/superseded'
        New-Item -ItemType Directory -Force -Path $archiveRoot | Out-Null
        $resolved = [IO.Path]::GetFullPath($file.FullName)
        if (-not $resolved.StartsWith((Join-Path $stage 'Content') + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected content path.' }
        Move-Item -LiteralPath $resolved -Destination (Join-Path $archiveRoot ([Guid]::NewGuid().ToString('N')+'-'+$file.Name))
    }
}
Copy-Item -LiteralPath (Join-Path $build 'stellar-continuum-native.exe') -Destination $stage
Copy-Item -LiteralPath (Join-Path $build 'SDL3.dll') -Destination $stage
[IO.File]::WriteAllText((Join-Path $stage 'cooked-only.marker'), "Source fallback is disabled for this portable build.`r`n")
$licenses = Join-Path $stage 'Licenses'
New-Item -ItemType Directory -Force -Path $licenses | Out-Null
foreach ($entry in @(@('SDL3','LICENSE.txt'),@('bc7enc','LICENSE'),@('bcdec','LICENSE'),@('nlohmann','LICENSE.MIT'))) {
    Copy-Item -LiteralPath (Join-Path $repo "third_party/$($entry[0])/$($entry[1])") -Destination (Join-Path $licenses "$($entry[0]).txt")
}
Get-ChildItem -LiteralPath (Join-Path $repo 'assets/visual/fonts') -File | Where-Object { $_.Name -match 'OFL|LICENSE' } | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $licenses $_.Name)
}
$player = @'
@echo off
setlocal
cd /d "%~dp0"
start "" "%~dp0stellar-continuum-native.exe"
'@
$developer = @'
@echo off
setlocal
cd /d "%~dp0"
start "" "%~dp0stellar-continuum-native.exe" --dev-game
'@
[IO.File]::WriteAllText((Join-Path $stage 'Play Game.cmd'),($player -replace "`r`n","`n" -replace "`n","`r`n"))
[IO.File]::WriteAllText((Join-Path $stage 'Developer Game.cmd'),($developer -replace "`r`n","`n" -replace "`n","`r`n"))
$readme = @'
Stellar Continuum - portable development preview

Extract the entire folder before playing.
Play Game.cmd starts the normal game.
Developer Game.cmd starts the developer game, with its own save folder.
Saves and settings are stored in %LOCALAPPDATA%\Stellar Continuum\NativePreview.
Developer saves are in its developer subfolder. Existing portable UserData
folders are never deleted; use the game's load screen to open those campaigns.
Existing saves can be opened from the game. Source art is not required.
All runtime content is in Content/*.stpak with a checksummed manifest.
Do not rename, remove or mix content packages from different builds.
Source fallback is disabled. A missing or corrupt package produces an error.

Game error/crash reports: %LOCALAPPDATA%\Stellar Continuum\Logs.
Each session records the game version, renderer and last view/zoom state.
Unexpected Windows faults also attempt a small crash dump. Reports stay local;
the newest eight logs/reports/dumps are retained. No upload is performed.

Current visual changes: faint star-map-style system skies, supplied major Sol
moons, separate satellite paths, slow planet rotation and camera tracking.
Orbits use mean elements and chart spacing. They are not dated ephemerides.

Performance update: shared survey/colony body lookups and reduced image memory.
Original artwork remains in the development master library. Verified rejected
runtime duplicates and superseded generated packages have been cleaned up.
Large-battle stress coverage and large-save latency still need further work.
Debug symbols and development tools are excluded from this download.
'@
[IO.File]::WriteAllText((Join-Path $stage 'README.txt'),($readme -replace "`r`n","`n" -replace "`n","`r`n"))
# Only runtime files are admitted. Never silently ZIP a development source tree.
$allowedTop = @('stellar-continuum-native.exe','SDL3.dll','StellarContinuumUninstall.exe','cooked-only.marker','Play Game.cmd','Developer Game.cmd','README.txt','package-files.json','release-manifest.json','Content','Licenses')
foreach ($item in Get-ChildItem -LiteralPath $stage) {
    if ($item.Name -notin $allowedTop) { throw "Unexpected shipping file: $($item.Name)" }
}
$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object { $_.Name -ne 'package-files.json' } | Sort-Object FullName | ForEach-Object {
    @{path=(Get-RelativePathCompat $stage $_.FullName).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
$versions = Get-Content -LiteralPath (Join-Path $repo 'export/runtime-config.json') -Raw | ConvertFrom-Json
$packageFiles = @($files | Where-Object {$_.path -like 'Content/*.stpak'})
$release = @{gameVersion=$versions.gameVersion;engineVersion=$versions.engineVersion;manifestVersion=1;cookerVersion='stellar-cooker-v1.0.0-20260920';platform='windows-x64';profile=$Profile;assetCount=(Get-Content -LiteralPath $validation -Raw | ConvertFrom-Json).assets;runtimeBytes=($files | Where-Object {$_.path -ne 'release-manifest.json'} | ForEach-Object {$_.bytes} | Measure-Object -Sum).Sum;packages=$packageFiles}
Write-Utf8NoBomFile (Join-Path $stage 'release-manifest.json') ($release | ConvertTo-Json -Depth 6)
$files = @($files | Where-Object {$_.path -ne 'release-manifest.json'}) + @{path='release-manifest.json';bytes=(Get-Item -LiteralPath (Join-Path $stage 'release-manifest.json')).Length;sha256=(Get-FileHash -LiteralPath (Join-Path $stage 'release-manifest.json')).Hash.ToLowerInvariant()}
Write-Utf8NoBomFile (Join-Path $stage 'package-files.json') (@{profile=$Profile;assets='cooked-only';files=$files} | ConvertTo-Json -Depth 6)
$sizeReport = @{runtimeBytes=(Get-ChildItem -LiteralPath $stage -File -Recurse | Measure-Object Length -Sum).Sum;packages=$packageFiles;previousRuntimeBytes=$null;packageDeltas=@();warnings=@()}
if ($PreviousReport) {
    $previous = Get-Content -LiteralPath $PreviousReport -Raw | ConvertFrom-Json
    $sizeReport.previousRuntimeBytes=$previous.runtimeBytes
    foreach ($package in $packageFiles) {
        $group=([IO.Path]::GetFileName($package.path) -split '-')[0]
        $before=@($previous.packages | Where-Object {([IO.Path]::GetFileName($_.path) -split '-')[0] -eq $group})
        $oldBytes=($before | ForEach-Object {$_.bytes} | Measure-Object -Sum).Sum
        $sizeReport.packageDeltas+=@{group=$group;beforeBytes=$oldBytes;afterBytes=$package.bytes;deltaBytes=$package.bytes-$oldBytes}
        if ($oldBytes -gt 0 -and $package.bytes -gt $oldBytes*1.10) {$sizeReport.warnings+="$group grew by more than 10%; review new assets before publishing."}
    }
}
Write-Utf8NoBomFile (Join-Path $repo "work/cooker/$Profile-size-report.json") ($sizeReport | ConvertTo-Json -Depth 6)
# Symbols remain available in a separate developer artifact, never in the game.
$symbolRoot=Join-Path $repo "work/cooker/symbols/$($versions.gameVersion)"
New-Item -ItemType Directory -Force -Path $symbolRoot | Out-Null
foreach ($name in @('stellar-continuum-native.pdb','SDL3.pdb')) {if (Test-Path -LiteralPath (Join-Path $build $name)) {Copy-Item -LiteralPath (Join-Path $build $name) -Destination $symbolRoot}}
Copy-Item -LiteralPath (Join-Path $stage 'release-manifest.json') -Destination $symbolRoot
if ($VerifyLaunch) {& (Join-Path $PSScriptRoot 'test-cooked-game.ps1') -PackageDirectory $stage}
if ($ArchivePath) {
    $archive = [IO.Path]::GetFullPath($ArchivePath)
    if (Test-Path -LiteralPath $archive) { throw 'Archive already exists; choose a new name.' }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $archive) | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($stage,$archive,[IO.Compression.CompressionLevel]::Optimal,$true)
    Get-Item -LiteralPath $archive | Select-Object FullName,Length
}
Write-Output "Cooked game ready: $stage"
