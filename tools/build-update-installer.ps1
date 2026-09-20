param(
    [Parameter(Mandatory=$true)][string]$BaseReleaseDirectory,
    [Parameter(Mandatory=$true)][string]$FullReleaseDirectory,
    [string]$BuildDirectory='build-native/preview',
    [string]$OutputDirectory='',
    [string]$ArchivePath='',
    [string]$SigningCertificateThumbprint='',
    [string]$TimestampUrl=''
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
function Resolve-ReleasePath([string]$Path){[IO.Path]::GetFullPath($(if([IO.Path]::IsPathRooted($Path)){$Path}else{Join-Path $repo $Path}))}
$base=Resolve-ReleasePath $BaseReleaseDirectory
$full=Resolve-ReleasePath $FullReleaseDirectory
$before=Get-Content -LiteralPath (Join-Path $base 'release-manifest.json') -Raw | ConvertFrom-Json
$after=Get-Content -LiteralPath (Join-Path $full 'release-manifest.json') -Raw | ConvertFrom-Json
$config=Get-Content -LiteralPath (Join-Path $repo 'export/runtime-config.json') -Raw | ConvertFrom-Json
if($after.gameVersion -ne $config.gameVersion -or $after.engineVersion -ne $config.engineVersion){throw 'The complete validated release must match the current build version.'}
if([version]($before.gameVersion.Split('-')[0]) -ge [version]($after.gameVersion.Split('-')[0])){throw 'Update must advance the numeric product version.'}
if($after.channel -ne 'dev' -and -not $SigningCertificateThumbprint){throw 'Beta/stable updates require Authenticode signing.'}
if($SigningCertificateThumbprint -and -not $TimestampUrl.StartsWith('https://')){throw 'Signing requires an HTTPS RFC3161 timestamp URL.'}
if(-not $OutputDirectory){$OutputDirectory=Join-Path $repo "work/download-release/StellarContinuum-Update-$($after.gameVersion)"}
$output=Resolve-ReleasePath $OutputDirectory
if(Test-Path -LiteralPath $output){throw 'Choose a fresh update output directory; existing releases are preserved.'}
$old=@{};foreach($f in $before.files){$old[$f.path]=$f}
$changed=@();$total=0L
# Hash the complete input before deriving the smaller download. A manifest alone
# is not evidence that the full release or copied changed bytes are intact.
foreach($f in $after.files){
    $path=Join-Path $full $f.path
    if((Get-Item -LiteralPath $path).Length -ne $f.bytes -or (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -ne $f.sha256){throw "Complete release validation failed: $($f.path)"}
    $prior=$old[$f.path]
    if(-not $prior -or $prior.sha256 -ne $f.sha256 -or $prior.bytes -ne $f.bytes){$changed+=$f;$total+=$f.bytes}
}
if(-not $changed.Count){throw 'No files changed.'}
$payload=Join-Path $output 'Payload';New-Item -ItemType Directory -Path $payload | Out-Null
foreach($f in $changed){$dest=Join-Path $payload $f.path;New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest) | Out-Null;Copy-Item -LiteralPath (Join-Path $full $f.path) -Destination $dest}
$after | Add-Member -NotePropertyName updateFrom -NotePropertyValue @{gameVersion=$before.gameVersion;buildId=$before.buildId} -Force
$after | Add-Member -NotePropertyName payloadPaths -NotePropertyValue @($changed.path) -Force
$after.maximumAdditionalBytes=$total+64MB
$manifest=Join-Path $payload 'release-manifest.json'
$after | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $manifest -Encoding utf8NoBOM
$cmake='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$build=Resolve-ReleasePath $BuildDirectory
& $cmake -S $repo -B $build "-DSTELLAR_SETUP_RELEASE_MANIFEST=$manifest"
if($LASTEXITCODE){throw 'Update configuration failed.'}
& $cmake --build $build --target StellarContinuumSetup --parallel 4
if($LASTEXITCODE){throw 'Update executable build failed.'}
$setup=Join-Path $output 'StellarContinuumSetup.exe';Copy-Item -LiteralPath (Join-Path $build 'installer-tools/StellarContinuumSetup.exe') -Destination $setup
if($SigningCertificateThumbprint){
    & signtool.exe sign /sha1 $SigningCertificateThumbprint /s My /fd SHA256 /tr $TimestampUrl /td SHA256 $setup
    if($LASTEXITCODE){throw 'Update signing failed.'}
    & signtool.exe verify /pa /all $setup
    if($LASTEXITCODE){throw 'Update signature validation failed.'}
}
$check=Start-Process -FilePath $setup -ArgumentList '--check-package' -WindowStyle Hidden -Wait -PassThru
if($check.ExitCode){throw 'Changed-file payload validation failed.'}
@"
STELLAR CONTINUUM — CHANGED-FILES UPDATE

From: $($before.gameVersion) / $($before.buildId)
To:   $($after.gameVersion) / $($after.buildId)

Close the game, extract this entire folder and run StellarContinuumSetup.exe.
Keep Payload beside it. Only changed files are included in this download.
Existing artwork is verified and retained. Saves and settings are preserved.
This update requires the exact installed base above. If an omitted artwork file
is missing or damaged, repair the base installation first; no files are changed
when this preflight fails. The same update can repair the files it contains.

Game crash/error reports: %LOCALAPPDATA%\Stellar Continuum\Logs
Setup reports: %LOCALAPPDATA%\Stellar Continuum\Installer\Logs
This development update is unsigned unless build-report.json says otherwise.
"@ | Set-Content -LiteralPath (Join-Path $output 'UPDATE.txt') -Encoding utf8NoBOM
@{gameVersion=$after.gameVersion;engineVersion=$after.engineVersion;buildId=$after.buildId;updateFrom=$after.updateFrom;changedFiles=$changed;changedBytes=$total;unchangedFiles=$after.files.Count-$changed.Count;setupSha256=(Get-FileHash -LiteralPath $setup).Hash.ToLowerInvariant();manifestSha256=(Get-FileHash -LiteralPath $manifest).Hash.ToLowerInvariant();authenticode=$(if($SigningCertificateThumbprint){'signed-and-verified'}else{'unsigned-development-build'});packageVerification='passed'} | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $output 'build-report.json') -Encoding utf8NoBOM
$symbols=Join-Path $repo "work/cooker/symbols/$($after.gameVersion)"
New-Item -ItemType Directory -Force -Path $symbols | Out-Null
Copy-Item -LiteralPath (Join-Path $build 'installer-tools/StellarContinuumSetup.pdb') -Destination (Join-Path $symbols 'StellarContinuumUpdate.pdb')
if($ArchivePath){
    $archive=[IO.Path]::GetFullPath($ArchivePath);if(Test-Path -LiteralPath $archive){throw 'Archive already exists.'}
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $archive) | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($output,$archive,[IO.Compression.CompressionLevel]::Optimal,$true)
    $hash=(Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant();"$hash  $([IO.Path]::GetFileName($archive))" | Set-Content -LiteralPath ($archive+'.sha256.txt') -Encoding ascii
    Get-Item -LiteralPath $archive | Select-Object FullName,Length
}
Write-Output "Changed-files update ready: $output"
