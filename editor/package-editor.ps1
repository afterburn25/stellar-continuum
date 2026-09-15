param(
    [Parameter(Mandatory=$true)][string]$EnginePackage,
    [Parameter(Mandatory=$true)][string]$Destination,
    [string]$TutorialAudio,
    [string]$VoiceAuditions
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$engineSource = (Resolve-Path -LiteralPath $EnginePackage).Path
$destinationPath = [IO.Path]::GetFullPath($Destination)
$manifest = Get-Content -LiteralPath (Join-Path $engineSource 'build-manifest.json') -Raw | ConvertFrom-Json
foreach ($file in $manifest.files) {
    $source = [IO.Path]::GetFullPath((Join-Path $engineSource $file.path))
    if (-not $source.StartsWith($engineSource + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid engine manifest path.' }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $file.sha256) { throw "Engine file failed its integrity check: $($file.path)" }
}
dotnet publish (Join-Path $PSScriptRoot 'Stellar.Engine.Editor/Stellar.Engine.Editor.csproj') -c Release -r win-x64 --self-contained true -p:DebugType=None -p:DebugSymbols=false -o $destinationPath --nologo
if ($LASTEXITCODE -ne 0) { throw 'Editor publish failed.' }
$runtime = Join-Path $destinationPath 'Engine'
[IO.Directory]::CreateDirectory($runtime) | Out-Null
foreach ($item in Get-ChildItem -LiteralPath $engineSource) { Copy-Item -LiteralPath $item.FullName -Destination $runtime -Recurse -Force }
$assets = Join-Path $repo 'assets'
$supported = @('.png','.jpg','.jpeg','.svg','.webp','.wav','.mp3','.ogg','.flac','.glb','.gltf','.obj','.json','.csv','.txt','.md')
foreach ($file in Get-ChildItem -LiteralPath $assets -File -Recurse | Where-Object { $_.Extension -in $supported }) {
    $target = Join-Path (Join-Path $destinationPath 'StarterAssets/Game') ([IO.Path]::GetRelativePath($assets, $file.FullName))
    [IO.Directory]::CreateDirectory((Split-Path -Parent $target)) | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $target -Force
}
foreach ($extra in @(@{Source=$TutorialAudio;Name='Officer Tutorial'}, @{Source=$VoiceAuditions;Name='Voice Auditions'})) {
    if ([string]::IsNullOrWhiteSpace($extra.Source)) { continue }
    $folder = Join-Path (Join-Path $destinationPath 'StarterAssets') $extra.Name
    [IO.Directory]::CreateDirectory($folder) | Out-Null
    foreach ($file in Get-ChildItem -LiteralPath $extra.Source -File | Where-Object { $_.Extension -in @('.wav','.json','.md') -and $_.Name -notin @('full-voiced-tutorial.wav','all-13-voices.wav') }) {
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $folder $file.Name) -Force
    }
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.txt') -Destination $destinationPath -Force
$sampleRoot = Join-Path $destinationPath 'Samples'
[IO.Directory]::CreateDirectory($sampleRoot) | Out-Null
$nativeCatalog = Join-Path $sampleRoot ([guid]::NewGuid().ToString('N') + '.json')
$receipt = & (Join-Path $runtime 'stellar-continuum.exe') --headless --generate-galaxy --seed-colonies --systems 500 --seed 8374837 --catalog-output $nativeCatalog | ConvertFrom-Json
if ($LASTEXITCODE -ne 0) { throw 'Sample world generation failed.' }
$sample = [ordered]@{
    SchemaVersion=1; Format='stellar-engine-editor-project'; Name='Stellar Continuum — First Light'; Seed=8374837; SystemCount=500
    Notes='A starting world for Stellar Continuum. Select a system to add design notes and bookmarks.'
    EngineVersion=$receipt.engineVersion; EngineCommit=$receipt.sourceCommit
    Catalog=(Get-Content -LiteralPath $nativeCatalog -Raw | ConvertFrom-Json)
    Annotations=@{'0'=@{DisplayName='Sol — First Light';Notes='Player arrival and officer introduction.';Bookmarked=$true}}
    Assets=@()
}
$sample | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath (Join-Path $sampleRoot 'First Light.stellar-project') -Encoding utf8NoBOM
Remove-Item -LiteralPath $nativeCatalog
Write-Output "Editor published to $destinationPath"
