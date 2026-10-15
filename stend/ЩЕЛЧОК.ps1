# Щелчок по чужому окну с честным выводом его на передний план.
#
# Windows не даёт фоновому процессу забрать фокус простым
# SetForegroundWindow -- вызов возвращает «истину», а окно остаётся под
# другими, и щелчок уходит в то, что лежит сверху. Лечится присоединением
# к очереди ввода текущего переднего окна (AttachThreadInput).
param(
    [Parameter(Mandatory = $true)][long]$Hwnd,
    [int]$X = 0,
    [int]$Y = 0,
    [int]$Razy = 1,
    [switch]$TolkoVpered
)
$ErrorActionPreference = 'Stop'
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Vvod {
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(IntPtr h, out int p);
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(int a, int b, bool f);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
    [DllImport("kernel32.dll")] public static extern int GetCurrentThreadId();
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public static bool Vpered(IntPtr h) {
        IntPtr f = GetForegroundWindow(); int pid;
        int chuzhoy = GetWindowThreadProcessId(f, out pid);
        int svoy = GetCurrentThreadId();
        AttachThreadInput(svoy, chuzhoy, true);
        ShowWindow(h, 9); BringWindowToTop(h);
        bool ok = SetForegroundWindow(h);
        AttachThreadInput(svoy, chuzhoy, false);
        return ok;
    }
}
"@
$h = [IntPtr]$Hwnd
[void][Vvod]::Vpered($h)
Start-Sleep -Milliseconds 350
if ($TolkoVpered) { "окно поднято"; exit 0 }
$r = New-Object Vvod+RECT
[void][Vvod]::GetWindowRect($h, [ref]$r)
[void][Vvod]::SetCursorPos($r.Left + $X, $r.Top + $Y)
Start-Sleep -Milliseconds 150
for ($i = 0; $i -lt $Razy; $i++) {
    [Vvod]::mouse_event(0x02, 0, 0, 0, [IntPtr]::Zero)
    [Vvod]::mouse_event(0x04, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 70
}
"щелчок ($Razy) по ($X,$Y) окна $Hwnd"
