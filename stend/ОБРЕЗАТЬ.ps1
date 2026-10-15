# Вырезать из снимка окна симулятора сам экран 240x320.
param(
    [Parameter(Mandatory = $true)][string]$Iz,
    [Parameter(Mandatory = $true)][string]$V,
    [int]$X = 263, [int]$Y = 90, [int]$W = 240, [int]$H = 320
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Bitmap]::FromFile($Iz)
$out = New-Object System.Drawing.Bitmap($W, $H)
$g = [System.Drawing.Graphics]::FromImage($out)
$g.DrawImage($src, (New-Object System.Drawing.Rectangle(0, 0, $W, $H)),
             (New-Object System.Drawing.Rectangle($X, $Y, $W, $H)),
             [System.Drawing.GraphicsUnit]::Pixel)
$g.Dispose(); $src.Dispose()
$out.Save($V, [System.Drawing.Imaging.ImageFormat]::Png); $out.Dispose()
"вырезано ${W}x${H} -> $V"
