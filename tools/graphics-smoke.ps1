param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [ValidateSet('auto', 'intel', 'nvidia', 'amd', 'warp')][string]$Adapter = 'auto',
    [string]$BuildDirectory = 'out\build',
    [switch]$RequireRayTracing
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not [System.IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $root $BuildDirectory }
$binaries = Join-Path $BuildDirectory $Configuration
$runtime = Join-Path $binaries 'VelosRuntime.exe'
$editor = Join-Path $binaries 'VelosEditor.exe'
$generator = Join-Path $binaries 'velos_graphics_fixture.exe'
foreach ($executable in @($runtime, $editor, $generator)) {
    if (-not (Test-Path -LiteralPath $executable)) { throw "Build $Configuration before graphics validation: $executable" }
}
$run = "$Configuration-$Adapter-$(Get-Date -Format 'yyyyMMdd-HHmmss')-$([guid]::NewGuid().ToString('N').Substring(0,8))"
$captures = Join-Path $root "out\validation\graphics\$run"
$fixtures = Join-Path $captures 'fixtures'
& $generator $fixtures
if ($LASTEXITCODE -ne 0) { throw 'Graphics fixture generation failed.' }
Add-Type -AssemblyName System.Drawing

function Invoke-RenderCheck {
    param([string]$Name, [string]$Scene, [string[]]$Options = @(), [string]$Executable = $runtime,
        [int]$Frames = 45, [switch]$Isolated)
    $arguments = @("--adapter=$Adapter", "--frames=$Frames", '--debug-gpu',
        "--capture=$captures\$Name.png", "--report=$captures\$Name.json")
    if ($Scene) { $arguments += "--scene=$Scene" }
    & $Executable @arguments @Options | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "GPU validation failed: $Name" }
    if (-not $Isolated) { & "$PSScriptRoot\verify-capture.ps1" -Path "$captures\$Name.png" | Out-Host }
    return (Get-Content -LiteralPath "$captures\$Name.json" -Raw | ConvertFrom-Json)
}

function Compare-RenderImages {
    param([string]$Reference, [string]$Actual, [ValidateSet('Exact', 'Spotlight', 'RayLod')][string]$Mode = 'Exact')
    $first = [System.Drawing.Bitmap]::FromFile("$captures\$Reference.png")
    $second = [System.Drawing.Bitmap]::FromFile("$captures\$Actual.png")
    try {
        if ($first.Size -ne $second.Size) { throw "Image sizes differ: $Reference / $Actual" }
        $different = 0
        $samples = 0
        $reduced = 0
        for ($row = 0; $row -lt $first.Height; $row += 2) {
            for ($column = 0; $column -lt $first.Width; $column += 2) {
                $expected = $first.GetPixel($column, $row)
                $observed = $second.GetPixel($column, $row)
                if ($Mode -eq 'RayLod' -and -not ($expected.R -gt $expected.G + 25 -and $expected.R -gt $expected.B + 30)) { continue }
                $samples++
                $delta = [Math]::Max([Math]::Abs([int]$expected.R - $observed.R),
                    [Math]::Max([Math]::Abs([int]$expected.G - $observed.G), [Math]::Abs([int]$expected.B - $observed.B)))
                $tolerance = if ($Mode -eq 'RayLod') { 8 } else { 1 }
                if ($delta -gt $tolerance) { $different++ }
                if ([int]$expected.R + $expected.G + $expected.B - $observed.R - $observed.G - $observed.B -gt 24) { $reduced++ }
            }
        }
        if ($Mode -eq 'Exact' -and $different -ne 0) { throw "$Actual differs from $Reference at $different pixels." }
        if ($Mode -eq 'Spotlight' -and $reduced -lt 200) { throw 'Spotlight cone did not reduce illumination outside its cone.' }
        if ($Mode -eq 'RayLod' -and ($samples -lt 40 -or $different -gt $samples * 0.025)) {
            throw "Ray/raster LOD self-shadow regression: $different / $samples sphere pixels differ."
        }
        Write-Output "PASS: $Mode image check ($different / $samples sampled pixels differ)."
    } finally { $first.Dispose(); $second.Dispose() }
}

$material = Invoke-RenderCheck -Name material -Scene "$fixtures\scene.velos" -Options @('--ray-shadows')
if ($material.ray_shadows_active -or $material.shadow_draws -eq 0) { throw 'Masked shadow casters must select raster shadows.' }
if ($material.dxr_supported -and $material.shadow_path -ne 'Raster (masked casters)') { throw 'Masked-caster fallback was not reported.' }
$rays = Invoke-RenderCheck -Name rays -Scene "$fixtures\ray-shadows.velos"
if ($RequireRayTracing -and -not $rays.dxr_supported) { throw 'This validation requires a hardware DXR 1.1 adapter.' }
if ($rays.dxr_supported) {
    if (-not $rays.ray_shadows_active -or $rays.shadow_draws -ne 0 -or $rays.dxr_memory_bytes -le 0) { throw 'Saved ray-shadow scene did not activate DXR.' }
} elseif ($rays.ray_shadows_active -or $rays.dxr_memory_bytes -ne 0 -or $rays.shadow_path -ne 'Raster (DXR unsupported)') {
    throw 'Unsupported adapters must report raster fallback without DXR allocations.'
}
$budget = Invoke-RenderCheck -Name budget -Scene "$fixtures\ray-shadows.velos" -Options @('--ray-budget-mb=0')
if ($budget.ray_shadows_active -or $budget.dxr_memory_bytes -ne 0 -or $budget.shadow_draws -eq 0) { throw 'Zero DXR budget must preserve raster shadows.' }
if ($budget.dxr_supported -and $budget.shadow_path -ne 'Raster (DXR memory budget)') { throw 'DXR budget fallback was not reported.' }
[void](Invoke-RenderCheck -Name spot -Scene "$fixtures\spotlight.velos")
[void](Invoke-RenderCheck -Name point -Scene "$fixtures\point-reference.velos")
Compare-RenderImages -Reference point -Actual spot -Mode Spotlight
if ($rays.ray_shadows_active) {
    $detail = Invoke-RenderCheck -Name ray-lod -Scene "$fixtures\ray-lod.velos" -Isolated
    [void](Invoke-RenderCheck -Name ray-lod-reference -Scene "$fixtures\ray-lod-reference.velos" -Isolated)
    if (-not $detail.ray_shadows_active -or $detail.lod_triangles_saved -eq 0) { throw 'The DXR LOD fixture must exercise a simplified mesh.' }
    Compare-RenderImages -Reference ray-lod-reference -Actual ray-lod -Mode RayLod
}
$reference = Invoke-RenderCheck -Name unbatched -Options @('--stress=1000', '--no-instancing', '--no-lods') -Frames 4
$instanced = Invoke-RenderCheck -Name instanced -Options @('--stress=1000', '--no-lods') -Frames 4
$detail = Invoke-RenderCheck -Name instanced-lods -Options @('--stress=1000') -Frames 4
if ($instanced.camera_draws -ge $reference.camera_draws -or $instanced.shadow_draws -ge $reference.shadow_draws) { throw 'Instancing must reduce camera and shadow draws.' }
if ($detail.lod_triangles_saved -eq 0 -or $detail.triangles -ge $instanced.triangles) { throw 'LOD selection must reduce submitted triangles.' }
Compare-RenderImages -Reference unbatched -Actual instanced
foreach ($scene in @(@('material', 'scene.velos'), @('rays', 'ray-shadows.velos'))) {
    $package = Join-Path $captures "package-$($scene[0])"
    & $runtime "--package=$fixtures\$($scene[1])" "--output=$package"
    if ($LASTEXITCODE -ne 0) { throw "Scene export failed: $($scene[0])" }
    $options = if ($scene[0] -eq 'material') { @('--ray-shadows') } else { @() }
    $exported = Invoke-RenderCheck -Name "$($scene[0])-export" -Scene "$package\game.velos" -Executable "$package\VelosRuntime.exe" -Options $options
    if ($exported.ray_shadows_active -ne ($scene[0] -eq 'rays' -and $rays.ray_shadows_active)) { throw 'Exported scene shadow policy changed.' }
    Compare-RenderImages -Reference $scene[0] -Actual "$($scene[0])-export"
}
foreach ($size in @(@(1600,1000), @(1024,720))) {
    $capture = "$captures\editor-$($size[0]).png"
    & $editor --self-test "--adapter=$Adapter" --debug-gpu "--scene=$fixtures\ray-shadows.velos" "--width=$($size[0])" "--height=$($size[1])" "--capture=$capture"
    if ($LASTEXITCODE -ne 0) { throw 'Ray-shadow editor authoring/resize check failed.' }
    & "$PSScriptRoot\verify-capture.ps1" -Path $capture
}
Write-Output "PASS: $Configuration graphics checks on $Adapter. Evidence: $captures"