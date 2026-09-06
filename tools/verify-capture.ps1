param(
    [Parameter(Mandatory = $true)][string]$Path,
    [ValidateRange(2, 65536)][int]$MinimumColors = 100
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$bitmap = [System.Drawing.Bitmap]::FromFile((Resolve-Path -LiteralPath $Path).Path)
try {
    $mint = 0
    $warm = 0
    $colors = New-Object 'System.Collections.Generic.HashSet[int]'
    for ($row = [int]($bitmap.Height * 0.2); $row -lt $bitmap.Height * 0.8; $row += 4) {
        for ($column = [int]($bitmap.Width * 0.2); $column -lt $bitmap.Width * 0.78; $column += 4) {
            $pixel = $bitmap.GetPixel($column, $row)
            [void]$colors.Add($pixel.ToArgb())
            if ($pixel.G -gt $pixel.R + 25 -and $pixel.B -gt $pixel.R + 12) { $mint++ }
            if ($pixel.R -gt $pixel.G + 25 -and $pixel.R -gt $pixel.B + 30) { $warm++ }
        }
    }
    if ($colors.Count -lt $MinimumColors -or $mint -lt 40 -or $warm -lt 40) {
        throw "Capture is blank or the reference scene is missing: colors=$($colors.Count), mint=$mint, warm=$warm."
    }
    Write-Output "PASS: $($bitmap.Width)x$($bitmap.Height) capture, $($colors.Count) colors, visible reference geometry."
} finally {
    $bitmap.Dispose()
}