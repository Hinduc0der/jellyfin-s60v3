# Один шаг по приложению в симуляторе: нажать клавиши и снять экран.
#
#   shag.ps1 -Knopki '{DOWN}','{ENTER}' -Fayl s1.png
param(
    [string[]]$Knopki = @(),
    [string]$Fayl = 'shag.png',
    [int]$Pauza = 900,
    [switch]$Skin
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class Sh {
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(IntPtr h, out int p);
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(int a, int b, bool f);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
    [DllImport("kernel32.dll")] public static extern int GetCurrentThreadId();
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public delegate bool Cb(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumWindows(Cb f, IntPtr p);
    public static IntPtr Nayti(string imya) {
        IntPtr n = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr p) {
            if (!IsWindowVisible(h)) return true;
            var t = new StringBuilder(256); GetWindowText(h, t, 256);
            if (t.ToString() == imya) { n = h; return false; }
            return true;
        }, IntPtr.Zero);
        return n;
    }
    public static void Vpered(IntPtr h) {
        IntPtr f = GetForegroundWindow(); int pid;
        int chuzhoy = GetWindowThreadProcessId(f, out pid);
        int svoy = GetCurrentThreadId();
        AttachThreadInput(svoy, chuzhoy, true);
        ShowWindow(h, 9); BringWindowToTop(h); SetForegroundWindow(h);
        AttachThreadInput(svoy, chuzhoy, false);
    }
}
"@
$h = [Sh]::Nayti('Qt Simulator')
if ($h -eq [IntPtr]::Zero) { throw "окно симулятора не найдено" }
$shell = New-Object -ComObject WScript.Shell
foreach ($k in $Knopki) {
    [Sh]::Vpered($h); Start-Sleep -Milliseconds 200
    $shell.SendKeys($k)
    Start-Sleep -Milliseconds $Pauza
}
[Sh]::Vpered($h); Start-Sleep -Milliseconds 400
$r = New-Object Sh+RECT
[void][Sh]::GetWindowRect($h, [ref]$r)
$ow = $r.Right - $r.Left; $ot = $r.Bottom - $r.Top
$okno = New-Object System.Drawing.Bitmap($ow, $ot)
$g = [System.Drawing.Graphics]::FromImage($okno)
$dc = $g.GetHdc(); [void][Sh]::PrintWindow($h, $dc, 2); $g.ReleaseHdc($dc); $g.Dispose()
if ($Skin) { $x = 230; $y = 10; $w = 310; $t = 740 }
else       { $x = 263; $y = 154; $w = 240; $t = 256 }
$out = New-Object System.Drawing.Bitmap($w, $t)
$g2 = [System.Drawing.Graphics]::FromImage($out)
$g2.DrawImage($okno, (New-Object System.Drawing.Rectangle(0, 0, $w, $t)),
              (New-Object System.Drawing.Rectangle($x, $y, $w, $t)),
              [System.Drawing.GraphicsUnit]::Pixel)
$g2.Dispose(); $okno.Dispose()
$out.Save($Fayl, [System.Drawing.Imaging.ImageFormat]::Png); $out.Dispose()
"снято -> $Fayl"
