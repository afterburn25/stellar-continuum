param([switch]$Apply, [string]$ReportPath = 'work/cooker/obsolete-cleanup.json')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$runtime = Get-Content -LiteralPath (Join-Path $repo 'work/cooker/release-report.json') -Raw | ConvertFrom-Json
$active = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($asset in $runtime.assets) { [void]$active.Add($asset.id); [void]$active.Add($asset.source) }
$entries = [Collections.Generic.List[object]]::new()
function CheckedPath([string]$relative, [string]$scope) {
    $path = [IO.Path]::GetFullPath((Join-Path $repo $relative))
    $boundary = [IO.Path]::GetFullPath((Join-Path $repo $scope)) + [IO.Path]::DirectorySeparatorChar
    if (!$path.StartsWith($boundary,[StringComparison]::OrdinalIgnoreCase)) { throw "Cleanup target escaped its scope: $path" }
    for ($item = Get-Item -LiteralPath $path; $item.FullName -ne $repo; $item = $item.Parent) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Cleanup refuses a linked path: $path" }
        if ($item -is [IO.FileInfo]) { $item = $item.Directory; if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked directory: $path" } }
    }
    return $path
}
$audit = Get-Content -LiteralPath (Join-Path $repo 'data/stellar/starfield-asset-audit-v1.json') -Raw | ConvertFrom-Json
foreach ($image in $audit.images) {
    if ($image.status -ne 'rejected' -or !$image.path -or !(Test-Path -LiteralPath (Join-Path $repo $image.path))) { continue }
    if ($active.Contains($image.path)) { throw "Rejected image is still a runtime dependency: $($image.path)" }
    $path = CheckedPath $image.path 'assets/visual/starfields'
    if (!(Test-Path -LiteralPath $image.source -PathType Leaf)) { throw "Original art is missing: $($image.source)" }
    $copyHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    $masterHash = (Get-FileHash -LiteralPath $image.source -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($copyHash -ne $image.sha256 -or $copyHash -ne $masterHash) { throw "Original/copy hash mismatch: $path" }
    $entries.Add(@{path=$path;bytes=(Get-Item -LiteralPath $path).Length;sha256=$copyHash;reason='Rejected duplicate runtime copy; identical original retained';preservedOriginal=$image.source})
}
# Only the build script's explicitly superseded, renamed package generations.
# These files are outside every Content directory; current packages are untouched.
$superseded = Join-Path $repo 'work/cooker/superseded'
if (Test-Path -LiteralPath $superseded) {
    foreach ($file in Get-ChildItem -LiteralPath $superseded -File) {
        if ($file.Name -notmatch '^[a-f0-9]{32}-((Audio|Backgrounds|Celestial|Core|Ships|UI|VFX)(-[a-f0-9]+)?\.stpak|runtime\.stmanifest\.bak)$') { continue }
        $path = CheckedPath ([IO.Path]::GetRelativePath($repo,$file.FullName)) 'work/cooker/superseded'
        $entries.Add(@{path=$path;bytes=$file.Length;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant();reason='Superseded generated content package'})
    }
}
$report = @{applied=[bool]$Apply;count=$entries.Count;bytes=($entries | Measure-Object bytes -Sum).Sum;files=@($entries.ToArray());preserved='Source art, audits, current packages, older downloads, saves and build inputs'}
$output = [IO.Path]::GetFullPath((Join-Path $repo $ReportPath))
if (!$output.StartsWith($repo+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Cleanup report must remain in this repository.' }
[IO.Directory]::CreateDirectory((Split-Path -Parent $output)) | Out-Null
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $output -Encoding utf8
if ($Apply) {
    foreach ($entry in $entries) {
        if ((Get-FileHash -LiteralPath $entry.path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) { throw "Target changed after audit: $($entry.path)" }
        Remove-Item -LiteralPath $entry.path
    }
}
[pscustomobject]@{applied=[bool]$Apply;files=$report.count;bytes=$report.bytes;report=$output}
