param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [ValidateSet('auto', 'intel', 'nvidia', 'amd', 'warp')][string]$Adapter = 'auto',
    [string]$BuildDirectory = 'out\build'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not [System.IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $root $BuildDirectory }
$binaries = Join-Path $BuildDirectory $Configuration
$editor = Join-Path $binaries 'VelosEditor.exe'
$runtime = Join-Path $binaries 'VelosRuntime.exe'
if (-not (Test-Path -LiteralPath $editor) -or -not (Test-Path -LiteralPath $runtime)) {
    throw "Build $Configuration first with tools/build.ps1."
}
$captures = Join-Path $root "out\validation\$(Split-Path $BuildDirectory -Leaf)\$Configuration-$Adapter"
[void][System.IO.Directory]::CreateDirectory($captures)
& $editor --self-test "--adapter=$Adapter" --debug-gpu "--capture=$captures\editor.png"
if ($LASTEXITCODE -ne 0) { throw 'Native editor authoring/resize smoke test failed.' }
& "$PSScriptRoot\verify-capture.ps1" -Path "$captures\editor.png"
& $editor --self-test "--adapter=$Adapter" --debug-gpu --width=1024 --height=720 "--capture=$captures\editor-small.png"
if ($LASTEXITCODE -ne 0) { throw 'Small-window editor smoke test failed.' }
& "$PSScriptRoot\verify-capture.ps1" -Path "$captures\editor-small.png"
& $runtime --frames=120 "--adapter=$Adapter" --debug-gpu "--capture=$captures\runtime.png"
if ($LASTEXITCODE -ne 0) { throw 'Standalone runtime smoke test failed.' }
& "$PSScriptRoot\verify-capture.ps1" -Path "$captures\runtime.png"
Write-Output "PASS: $Configuration native editor/runtime smoke checks on $Adapter. Captures: $captures"