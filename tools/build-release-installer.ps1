param(
    [string]$BuildDirectory = 'build-native/preview',
    [string]$OutputDirectory = '',
    [string]$CookedSource = '',
    [string]$ArchivePath = '',
    [string]$SigningCertificateThumbprint = '',
    [string]$TimestampUrl = '',
    [switch]$SkipBuild,
    [switch]$ReuseCookedOutput,
    [switch]$VerifyLaunch
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$versions = Get-Content -LiteralPath (Join-Path $repo 'export/runtime-config.json') -Raw | ConvertFrom-Json
if ($versions.gameVersion -notmatch '^([0-9]+)\.([0-9]+)\.([0-9]+)\.([0-9]+)-(dev|beta|stable)$') {throw 'Invalid authoritative game version.'}
$numeric = $versions.gameVersion.Split('-')[0]
$channel = $Matches[5]
foreach ($part in $numeric.Split('.')) {if ([uint32]$part -gt 65535 -or ($part.Length -gt 1 -and $part.StartsWith('0'))) {throw 'Invalid Windows version component.'}}
if ($channel -ne 'dev' -and -not $SigningCertificateThumbprint) {throw 'Beta/stable publication requires an Authenticode signing certificate. Private keys must remain in the Windows certificate store.'}
if ($SigningCertificateThumbprint -and (-not $TimestampUrl.StartsWith('https://'))) {throw 'Supply an HTTPS RFC3161 timestamp URL for signing.'}
if (-not $OutputDirectory) {$OutputDirectory="work/download-release/StellarContinuum-Setup-$($versions.gameVersion)"}
$output = [IO.Path]::GetFullPath($(if([IO.Path]::IsPathRooted($OutputDirectory)){$OutputDirectory}else{Join-Path $repo $OutputDirectory}))
$build = [IO.Path]::GetFullPath($(if([IO.Path]::IsPathRooted($BuildDirectory)){$BuildDirectory}else{Join-Path $repo $BuildDirectory}))
$payload = Join-Path $output 'Payload'
if($ReuseCookedOutput -and $CookedSource){throw 'Choose either a cooked source or reuse of the existing output.'}
if($ReuseCookedOutput -and -not (Test-Path -LiteralPath (Join-Path $payload 'Content/runtime.stmanifest'))){throw 'There is no cooked output to reuse.'}
if ($output -eq $repo -or $output -eq $build -or $repo.StartsWith($output+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {throw 'Use a separate release staging folder.'}
New-Item -ItemType Directory -Force -Path $payload | Out-Null
$cmake='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
if (-not $SkipBuild) {
    & $cmake --build $build --target stellar-continuum-native StellarCooker StellarContinuumUninstall stellar_maintenance_tests stellar_windows_maintenance_tests --parallel 4
    if ($LASTEXITCODE) {throw 'Native build failed. Use a Visual Studio developer shell.'}
} else {
    & $cmake --build $build --target StellarContinuumUninstall stellar_maintenance_tests stellar_windows_maintenance_tests --parallel 4
    if ($LASTEXITCODE) {throw 'Maintenance build failed.'}
}
# A supplied cooked source is validated by the actual cooker below. Copy only
# its active shipping files; never copy source art, reports, symbols or saves.
if ($CookedSource) {
    $source=[IO.Path]::GetFullPath($CookedSource)
    if ($source -eq $payload) {throw 'CookedSource must differ from output Payload.'}
    foreach ($name in @('Content','Licenses')) {
        $destination=Join-Path $payload $name
        New-Item -ItemType Directory -Force -Path $destination | Out-Null
        Get-ChildItem -LiteralPath (Join-Path $source $name) -File | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $destination}
    }
}
& (Join-Path $PSScriptRoot 'build-cooked-game.ps1') -BuildDirectory $BuildDirectory -OutputDirectory $payload -SkipBuild -SkipCook:([bool]$CookedSource -or $ReuseCookedOutput)
if ($LASTEXITCODE) {throw 'Cooked release validation failed.'}
Copy-Item -LiteralPath (Join-Path $build 'installer-tools/StellarContinuumUninstall.exe') -Destination $payload
function Sign-ReleaseFile([string]$Path) {
    if (-not $SigningCertificateThumbprint) {return}
    & signtool.exe sign /sha1 $SigningCertificateThumbprint /s My /fd SHA256 /tr $TimestampUrl /td SHA256 $Path
    if ($LASTEXITCODE) {throw "Signing failed: $Path"}
    & signtool.exe verify /pa /all $Path
    if ($LASTEXITCODE) {throw "Signature validation failed: $Path"}
}
foreach ($name in @('stellar-continuum-native.exe','StellarContinuumUninstall.exe')) {
    $file=Join-Path $payload $name
    $info=[Diagnostics.FileVersionInfo]::GetVersionInfo($file)
    if ($info.FileVersion -ne $numeric -or $info.ProductVersion -ne $versions.gameVersion) {throw "Executable/manifest version mismatch: $name"}
    Sign-ReleaseFile $file
}
$allowedTop=@('stellar-continuum-native.exe','SDL3.dll','StellarContinuumUninstall.exe','cooked-only.marker','Play Game.cmd','Developer Game.cmd','README.txt','package-files.json','release-manifest.json','Content','Licenses')
foreach ($file in Get-ChildItem -LiteralPath $payload) {if ($file.Name -notin $allowedTop) {throw "Unapproved payload: $($file.Name)"}}
$files=@(Get-ChildItem -LiteralPath $payload -File -Recurse | Where-Object {$_.Name -notin @('release-manifest.json','package-files.json')} | Sort-Object FullName | ForEach-Object {
    if ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) {throw 'Release payload cannot contain reparse points.'}
    $path=[IO.Path]::GetRelativePath($payload,$_.FullName).Replace('\','/')
    $package=if($path -like 'Content/*.stpak'){([IO.Path]::GetFileNameWithoutExtension($path) -split '-')[0]}else{'Runtime'}
    @{path=$path;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant();package=$package}
})
$runtimeBytes=[uint64]($files | Measure-Object bytes -Sum).Sum
$identity=[Text.Encoding]::UTF8.GetBytes(($files | ForEach-Object {$_.path+':'+$_.sha256}) -join "`n")
$hash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($identity)).ToLowerInvariant()
$buildId="$($versions.gameVersion)-$($hash.Substring(0,16))"
$validation=Get-Content -LiteralPath (Join-Path $repo 'work/cooker/release-validation.json') -Raw | ConvertFrom-Json
$release=@{manifestVersion=2;productId='StellarContinuum';gameVersion=$versions.gameVersion;engineVersion=$versions.engineVersion;channel=$channel;buildId=$buildId;platform='windows-x64';assets='cooked-only';cookerVersion='stellar-cooker-v1.0.0-20260920';assetCount=$validation.assets;runtimeBytes=$runtimeBytes;maximumAdditionalBytes=$runtimeBytes+64MB;gameExecutableHash=($files | Where-Object path -eq 'stellar-continuum-native.exe').sha256;packages=@($files | Where-Object {$_.path -like 'Content/*.stpak'});files=$files}
$manifest=Join-Path $payload 'release-manifest.json'
$release | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifest -Encoding utf8NoBOM
$indexedFiles=@($files)+@{path='release-manifest.json';bytes=(Get-Item -LiteralPath $manifest).Length;sha256=(Get-FileHash -LiteralPath $manifest).Hash.ToLowerInvariant();package='Metadata'}
@{profile='release';assets='cooked-only';files=$indexedFiles} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $payload 'package-files.json') -Encoding utf8NoBOM
# Rebuild bootstrapper only after the final signed binaries and manifest exist.
# The metadata SHA256 is compiled into setup, then setup itself is signed.
& $cmake -S $repo -B $build "-DSTELLAR_SETUP_RELEASE_MANIFEST=$manifest"
if ($LASTEXITCODE) {throw 'Installer configuration failed.'}
& $cmake --build $build --target StellarContinuumSetup --parallel 4
if ($LASTEXITCODE) {throw 'Installer build failed.'}
$setup=Join-Path $output 'StellarContinuumSetup.exe'
Copy-Item -LiteralPath (Join-Path $build 'installer-tools/StellarContinuumSetup.exe') -Destination $setup
Sign-ReleaseFile $setup
$symbols=Join-Path $repo "work/cooker/symbols/$($versions.gameVersion)"
New-Item -ItemType Directory -Force -Path $symbols | Out-Null
foreach($name in @('StellarContinuumSetup.pdb','StellarContinuumUninstall.pdb')) {if(Test-Path -LiteralPath (Join-Path $build "installer-tools/$name")){Copy-Item -LiteralPath (Join-Path $build "installer-tools/$name") -Destination $symbols}}
Copy-Item -LiteralPath $manifest -Destination $symbols
$test=Join-Path $build 'installer/stellar_maintenance_tests.exe'
& $test (Join-Path $repo ('work/installer-20260920/release-tests-'+[Guid]::NewGuid().ToString('N')))
if ($LASTEXITCODE) {throw 'Maintenance regression tests failed; release publication stopped.'}
& (Join-Path $build 'installer/stellar_windows_maintenance_tests.exe') (Join-Path $repo 'work/installer-20260920/release-os-tests')
if ($LASTEXITCODE) {throw 'Windows maintenance regression tests failed; release publication stopped.'}
$check=Start-Process -FilePath $setup -ArgumentList '--check-package' -WindowStyle Hidden -Wait -PassThru
if ($check.ExitCode) {throw 'Final setup/payload verification failed.'}
if ($VerifyLaunch) {& (Join-Path $PSScriptRoot 'test-cooked-game.ps1') -PackageDirectory $payload}
$report=@{buildId=$buildId;gameVersion=$versions.gameVersion;engineVersion=$versions.engineVersion;channel=$channel;platform='windows-x64';scope='current-user';payloadBytes=$runtimeBytes;setupBytes=(Get-Item -LiteralPath $setup).Length;manifestSha256=(Get-FileHash -LiteralPath $manifest).Hash.ToLowerInvariant();setupSha256=(Get-FileHash -LiteralPath $setup).Hash.ToLowerInvariant();authenticode=if($SigningCertificateThumbprint){'signed-and-verified'}else{'unsigned-development-build'};assetCount=$validation.assets;packages=$release.packages;maintenanceTests='passed';offlinePackageVerification='passed';onlineUpdates='not configured';binaryDeltaPatches='not implemented'}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'build-report.json') -Encoding utf8NoBOM
@'
STELLAR CONTINUUM — WINDOWS SETUP

Extract this entire folder, then run StellarContinuumSetup.exe.
Keep its Payload folder beside it. Internet access is not required.
The same setup automatically offers Install, Update or Repair and blocks older
versions from replacing newer installations. No administrator access is needed.
Choose a drive with enough space. The Start menu always gets a game shortcut;
desktop and Developer Game shortcuts are optional.

Saves/settings: %LOCALAPPDATA%\Stellar Continuum\NativePreview
Developer saves: the developer subfolder at that location.
Setup logs: %LOCALAPPDATA%\Stellar Continuum\Installer\Logs
Uninstall: Windows Settings > Apps > Installed apps > Stellar Continuum.
Saves, settings, mods and unlisted files are preserved during maintenance.

This development build is unsigned unless build-report.json says otherwise.
No online update service, automatic telemetry or binary delta patching is used.
Future signed releases use the same package manifest and version comparison.
'@ | Set-Content -LiteralPath (Join-Path $output 'INSTALL.txt') -Encoding utf8NoBOM
if ($ArchivePath) {
    $archive=[IO.Path]::GetFullPath($ArchivePath)
    if (Test-Path -LiteralPath $archive) {throw 'Choose a new archive name; existing releases are preserved.'}
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $archive) | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($output,$archive,[IO.Compression.CompressionLevel]::Optimal,$true)
    Get-FileHash -LiteralPath $archive -Algorithm SHA256 | Format-List
    Get-Item -LiteralPath $archive | Select-Object FullName,Length
}
Write-Output "Offline installer ready: $output"
