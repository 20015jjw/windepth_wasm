# Launch windepth.exe, let it run for a moment, grab its window and close it.
#
#   powershell -ExecutionPolicy Bypass -File tools/shot.ps1 tests/native.png 4
#
# Used to confirm the Win32 DIB path actually paints; the headless harness in
# tests/frames.exe covers everything above it.
param(
    [string]$Out = "tests/native.png",
    [int]$Seconds = 4,
    [string]$Exe = "windepth.exe",
    # SendKeys string pressed once the window is up, then $After more seconds
    # before the grab.  "{F2}" starts a game; "z" drops a charge.
    [string]$Keys = "",
    [int]$After = 0
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out R r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref P p);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    public struct R { public int left, top, right, bottom; }
    public struct P { public int x, y; }
}
"@

$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
$proc = Start-Process -FilePath (Join-Path $root $Exe) -PassThru
Start-Sleep -Seconds $Seconds
$proc.Refresh()
$h = $proc.MainWindowHandle
if ($h -eq [IntPtr]::Zero) {
    Write-Error "no window"
    $proc.Kill(); Pop-Location; exit 1
}
[void][W]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 400
if ($Keys -ne "") {
    $sh = New-Object -ComObject WScript.Shell
    $sh.SendKeys($Keys)
    Start-Sleep -Seconds $After
}
$r = New-Object W+R
[void][W]::GetClientRect($h, [ref]$r)
$p = New-Object W+P
$p.x = 0; $p.y = 0
[void][W]::ClientToScreen($h, [ref]$p)
$w = $r.right - $r.left
$hh = $r.bottom - $r.top
$bmp = New-Object System.Drawing.Bitmap $w, $hh
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$gfx.CopyFromScreen($p.x, $p.y, 0, 0, (New-Object System.Drawing.Size $w, $hh))
$bmp.Save((Join-Path $root $Out), [System.Drawing.Imaging.ImageFormat]::Png)
$gfx.Dispose(); $bmp.Dispose()
Write-Output "saved $Out  ${w}x${hh}"
$proc.Kill()
Pop-Location
