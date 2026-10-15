# Снять чужое окно, даже если оно закрыто другими.
#
# SetForegroundWindow из фонового процесса Windows игнорирует (защита от
# перехвата фокуса), поэтому обычный снимок экрана отдавал то, что лежит
# сверху. PrintWindow просит окно нарисовать себя в наш контекст -- чужие
# окна в кадр не попадают.
param(
    [Parameter(Mandatory = $true)][long]$Hwnd,
    [Parameter(Mandatory = $true)][string]$Fayl,
    # 2 = PW_RENDERFULLCONTENT: нужен для окон, которые рисуют себя сами
    # (OpenGL, DirectComposition).
    [uint32]$Flag = 2
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Pechat {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$h = [IntPtr]$Hwnd
$r = New-Object Pechat+RECT
[void][Pechat]::GetWindowRect($h, [ref]$r)
$w = $r.Right - $r.Left; $t = $r.Bottom - $r.Top
$bmp = New-Object System.Drawing.Bitmap($w, $t)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
$ok = [Pechat]::PrintWindow($h, $dc, $Flag)
$g.ReleaseHdc($dc); $g.Dispose()
$bmp.Save($Fayl, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
"снято $($w)x$($t) (PrintWindow=$ok) -> $Fayl"
