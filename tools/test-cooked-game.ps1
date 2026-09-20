param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [string]$SaveSource = '',
    [ValidateSet('startup','system','planetary')][string]$Scenario = 'startup',
    [string]$ReportDirectory = ''
)
$ErrorActionPreference='Stop'
$package=[IO.Path]::GetFullPath($PackageDirectory)
if (-not (Test-Path -LiteralPath (Join-Path $package 'cooked-only.marker'))) {throw 'This check requires a cooked-only package.'}
if (-not $ReportDirectory) {$ReportDirectory=Join-Path (Split-Path -Parent $PSScriptRoot) ('work/cooker/launch-'+[Guid]::NewGuid().ToString('N'))}
$output=[IO.Path]::GetFullPath($ReportDirectory)
if (Test-Path -LiteralPath $output) {throw 'Choose a fresh report directory; existing saves will not be overwritten.'}
New-Item -ItemType Directory -Path $output | Out-Null
$isPlayer=$SaveSource -and $SaveSource.EndsWith('.player17.json',[StringComparison]::OrdinalIgnoreCase)
$save=Join-Path $output $(if($isPlayer){'campaign.player17.json'}else{'campaign.dev17.json'})
if ($SaveSource) {Copy-Item -LiteralPath $SaveSource -Destination $save}
elseif ($Scenario -ne 'startup') {throw 'System and planetary checks require an isolated source save.'}
else {[IO.File]::WriteAllText($save,'{}')}
$capture=Join-Path $output 'game.png'
$launchArgs=@('--save-path',$save,'--width','1280','--height','720')
if (-not $isPlayer) {$launchArgs+= '--dev-game'}
if ($Scenario -eq 'startup') {$launchArgs+=@('--new-game-smoke',$capture,'--audio-check')}
else {$launchArgs+=@('--load',"--$Scenario-smoke",$capture)}
# Quote each argument for the Windows command line; none is evaluated by a shell.
$quoted=@($launchArgs | ForEach-Object {'"'+$_.Replace('"','\"')+'"'})
$timer=[Diagnostics.Stopwatch]::StartNew()
$process=Start-Process -FilePath (Join-Path $package 'stellar-continuum-native.exe') -ArgumentList $quoted -WorkingDirectory $output -WindowStyle Hidden -RedirectStandardOutput (Join-Path $output 'runtime.log') -RedirectStandardError (Join-Path $output 'stderr.log') -PassThru
$peak=0L
while (-not $process.HasExited) {
    $process.Refresh();$peak=[Math]::Max($peak,$process.PeakWorkingSet64)
    if ($timer.Elapsed.TotalMinutes -gt 10) {$process.Kill();throw 'Cooked launch check timed out.'}
    Start-Sleep -Milliseconds 200
}
$process.WaitForExit()
$log=Get-Content -LiteralPath (Join-Path $output 'runtime.log') -Raw
$statsLine=@($log -split "`n" | Where-Object {$_ -like 'cooked_assets=*'}) | Select-Object -Last 1
$stats=if ($statsLine) {$statsLine.Substring('cooked_assets='.Length) | ConvertFrom-Json} else {$null}
$report=@{scenario=$Scenario;exitCode=$process.ExitCode;elapsedSeconds=$timer.Elapsed.TotalSeconds;peakRamBytes=$peak;cooked=$stats}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output 'process.json') -Encoding utf8
if ($process.ExitCode -or -not $stats -or $stats.source_fallback -or $stats.failures -or -not (Test-Path -LiteralPath $capture)) {throw "Cooked game launch failed. Inspect $output"}
Write-Output "Cooked-only $Scenario check passed: $output"
