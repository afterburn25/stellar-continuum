param(
    [string]$OutputRoot = 'C:\Users\after\Documents\Codex\2026-09-12\le\outputs\Stellar-Alien-Fleet-0.1.0',
    [string]$LibraryRoot = 'C:\Users\after\Documents\Stellar Engine\Assets'
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$assetRoot = Join-Path $repoRoot 'assets\models\alien-fleet-v1'
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$LibraryRoot = [IO.Path]::GetFullPath($LibraryRoot)
$utf8 = [Text.UTF8Encoding]::new($false)
$validation = Get-Content -LiteralPath (Join-Path $assetRoot 'validation-report.json') -Raw | ConvertFrom-Json
$workshop = Get-Content -LiteralPath (Join-Path $assetRoot 'workshop-test-report.json') -Raw | ConvertFrom-Json
if ($validation.errors -ne 0 -or $workshop.failed -ne 0 -or $workshop.browserErrors.Count -ne 0) { throw 'Validation must pass before packaging.' }
if (Test-Path -LiteralPath $OutputRoot) { throw 'Output already exists. Use a new version directory; existing packages are retained.' }
[IO.Directory]::CreateDirectory($OutputRoot) | Out-Null
foreach ($file in Get-ChildItem -LiteralPath $assetRoot -File -Recurse) {
    $relative = [IO.Path]::GetRelativePath($assetRoot, $file.FullName)
    $destination = Join-Path $OutputRoot $relative
    [IO.Directory]::CreateDirectory((Split-Path -Parent $destination)) | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $destination
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs\content\ALIEN_FLEET.md') -Destination (Join-Path $OutputRoot 'README.md')
$sourceRoot = Join-Path $OutputRoot 'Source'
[IO.Directory]::CreateDirectory($sourceRoot) | Out-Null
foreach ($file in Get-ChildItem -LiteralPath $PSScriptRoot -File) { Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $sourceRoot $file.Name) }
$sourceNote = @'
Editable master source is included here. In the repository it lives at tools/alien-fleet.
For a standalone rebuild, make a workspace, copy these files to tools/alien-fleet,
run npm ci --prefix tools/alien-fleet, then node tools/alien-fleet/build.mjs.
The builder writes assets/models/alien-fleet-v1 relative to that workspace.
The delivered Alien-Fleet-Workshop.html opens directly without a build or Internet.
'@
[IO.File]::WriteAllText((Join-Path $sourceRoot 'SOURCE-README.txt'), $sourceNote, $utf8)
$indexPath = Join-Path $LibraryRoot 'library.json'
$indexBefore = [IO.File]::ReadAllText($indexPath)
$library = @($indexBefore | ConvertFrom-Json)
$beforeCount = $library.Count
$added = @()
$records = @()
$importFiles = @(Get-ChildItem -LiteralPath $assetRoot -File -Recurse | Where-Object Extension -in @('.glb','.png','.json','.txt')) + @(Get-Item -LiteralPath (Join-Path $OutputRoot 'README.md'))
foreach ($file in $importFiles) {
    if ($file.Length -gt 25MB) { throw "Asset exceeds library size limit: $($file.Name)" }
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $category = switch ($file.Extension) { '.glb' {'Models'} '.png' {'Images'} default {'Data'} }
    $relative = Join-Path (Join-Path $category $hash) $file.Name
    $destination = [IO.Path]::GetFullPath((Join-Path $LibraryRoot $relative))
    if (-not $destination.StartsWith($LibraryRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Destination escaped library.' }
    [IO.Directory]::CreateDirectory((Split-Path -Parent $destination)) | Out-Null
    if (Test-Path -LiteralPath $destination) {
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hash) { throw 'Existing asset hash mismatch.' }
    } else { Copy-Item -LiteralPath $file.FullName -Destination $destination }
    if (-not @($library | Where-Object { $_.Id -eq $hash -and $_.Name -eq $file.Name }).Count) {
        $library += [pscustomobject][ordered]@{Id=$hash;Name=$file.Name;Category=$category;Size=$file.Length;RelativePath=$relative;Description='Stellar Continuum / Alien Fleet 0.1.0 / Library candidate — species hulls and detachable equipment; game integration pending'}
        $added += $file.Name
    }
    $records += [ordered]@{name=$file.Name;category=$category;sha256=$hash;relativePath=$relative}
}
# Preserve all other work and reject a concurrent index change before replacing it.
if ([IO.File]::ReadAllText($indexPath) -cne $indexBefore) { throw 'Library changed during import; its index was preserved. Retry registration from the asset source.' }
if ($added.Count -gt 0) {
    $pending = Join-Path $LibraryRoot ('library-alien-fleet-' + [guid]::NewGuid().ToString('N') + '.pending')
    $backup = Join-Path $LibraryRoot ('library-before-alien-fleet-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff') + '.bak')
    [IO.File]::WriteAllText($pending, ($library | ConvertTo-Json -Depth 20), $utf8)
    [IO.File]::Replace($pending, $indexPath, $backup, $true)
}
foreach ($record in $records) {
    $file = Join-Path $LibraryRoot $record.relativePath
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $record.sha256) { throw 'Library copy integrity failed.' }
}
$receipt = [ordered]@{version='0.1.0';libraryRoot=$LibraryRoot;before=$beforeCount;after=$library.Count;added=$added.Count;assets=$records}
[IO.File]::WriteAllText((Join-Path $OutputRoot 'engine-library-receipt.json'), ($receipt | ConvertTo-Json -Depth 10), $utf8)
$packRecords = @(Get-ChildItem -LiteralPath $OutputRoot -File -Recurse | ForEach-Object {
    [ordered]@{path=[IO.Path]::GetRelativePath($OutputRoot,$_.FullName).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
[IO.File]::WriteAllText((Join-Path $OutputRoot 'package-manifest.json'), ([ordered]@{version='0.1.0';files=$packRecords} | ConvertTo-Json -Depth 10), $utf8)
$zip = $OutputRoot + '.zip'
if (Test-Path -LiteralPath $zip) { throw 'Package ZIP exists; retained without replacement.' }
Compress-Archive -LiteralPath $OutputRoot -DestinationPath $zip
[ordered]@{output=$OutputRoot;zip=$zip;zipBytes=(Get-Item -LiteralPath $zip).Length;libraryBefore=$beforeCount;libraryAfter=$library.Count;added=$added.Count;files=$packRecords.Count} | ConvertTo-Json
