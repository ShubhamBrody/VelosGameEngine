param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [switch]$Test,
    [switch]$Run,
    [string]$Adapter = 'auto'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Install Visual Studio with Desktop development with C++ and the Windows SDK.'
}
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'No Visual Studio C++ toolchain was found.' }
$version = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
$generator = if ([version]$version -ge [version]'18.0') { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' }
$cmake = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) {
    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if (-not $cmakeCommand) { throw 'Install CMake tools for Windows in the Visual Studio Installer.' }
    $cmake = $cmakeCommand.Source
}
$buildDirectory = Join-Path $root 'out\build'
& $cmake -S $root -B $buildDirectory -G $generator -A x64 -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmake --build $buildDirectory --config $Configuration --parallel 8
if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }
if ($Test) {
    & (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir $buildDirectory -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
}
if ($Run) {
    $editor = Join-Path $buildDirectory "$Configuration\VelosEditor.exe"
    if (-not (Test-Path -LiteralPath $editor)) { throw 'The editor target is not available in this checkout yet.' }
    Start-Process -FilePath $editor -ArgumentList "--adapter=$Adapter" -WorkingDirectory $root
}