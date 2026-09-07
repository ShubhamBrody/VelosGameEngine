param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [ValidateSet('auto', 'intel', 'nvidia', 'amd', 'warp')][string]$Adapter = 'auto',
    [string]$BuildDirectory = 'out\build-branding'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not [System.IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $root $BuildDirectory }
$binaries = Join-Path $BuildDirectory $Configuration
$editor = Join-Path $binaries 'VelosEditor.exe'
$runtime = Join-Path $binaries 'VelosRuntime.exe'
$splashTests = Join-Path $binaries 'velos_splash_tests.exe'
foreach ($executable in @($editor, $runtime, $splashTests)) {
    if (-not (Test-Path -LiteralPath $executable)) { throw "Build the branding targets first: $executable" }
}
$captures = Join-Path $root ('out\validation-branding\' + $Configuration + '-' + $Adapter + '-' + [guid]::NewGuid().ToString('N'))
[void][System.IO.Directory]::CreateDirectory($captures)
& $splashTests $captures
if ($LASTEXITCODE -ne 0) { throw 'Native splash resource/lifecycle tests failed.' }

function Invoke-StartupCheck {
    param([string]$Executable, [string[]]$Arguments, [string]$Expected)
    $output = & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Native startup failed: $Executable" }
    $output | Out-Host
    if (-not ($output -contains "Startup splash: $Expected")) { throw "Expected startup splash policy: $Expected" }
}

$scene = Join-Path $root 'samples\workshop\scene.velos'
Invoke-StartupCheck -Executable $editor -Expected shown -Arguments @('--self-test', '--splash', '--debug-gpu', "--adapter=$Adapter", "--scene=$scene",
    "--capture-splash=$captures\editor-splash.png", "--capture=$captures\editor-ready.png")
& "$PSScriptRoot\verify-capture.ps1" -Path "$captures\editor-ready.png"
Invoke-StartupCheck -Executable $editor -Expected skipped -Arguments @('--self-test', '--debug-gpu', "--adapter=$Adapter", "--scene=$scene")
Invoke-StartupCheck -Executable $runtime -Expected shown -Arguments @('--frames=12', '--splash', '--debug-gpu', "--adapter=$Adapter", "--scene=$scene",
    "--capture-splash=$captures\runtime-splash.png", "--capture=$captures\runtime-ready.png")
Invoke-StartupCheck -Executable $runtime -Expected skipped -Arguments @('--frames=12', '--splash', '--no-splash', '--debug-gpu', "--adapter=$Adapter", "--scene=$scene",
    "--capture=$captures\runtime-no-splash.png")
$package = Join-Path $captures 'export'
$packaged = & $runtime "--package=$scene" "--output=$package" --splash
if ($LASTEXITCODE -ne 0) { throw 'Branded runtime export failed.' }
$packaged | Out-Host
if ($packaged -match 'Startup splash: shown') { throw 'Packaging must not open a splash window.' }
Invoke-StartupCheck -Executable (Join-Path $package 'VelosRuntime.exe') -Expected shown -Arguments @('--frames=12', '--splash', '--debug-gpu', "--adapter=$Adapter",
    "--capture-splash=$captures\export-splash.png", "--capture=$captures\export-ready.png")
& node "$PSScriptRoot\branding\verify-captures.mjs" $captures
if ($LASTEXITCODE -ne 0) { throw 'Branded startup image validation failed.' }
Write-Output "PASS: $Configuration branded startup and export on $Adapter. Evidence: $captures"