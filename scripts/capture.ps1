# Capture the SentinelIDE window (or any window by -Class) to build\shot.png.
# DPI-aware PrintWindow (PW_RENDERFULLCONTENT) so the bitmap matches real pixels.
# The app can't be added to the screenshot-MCP allowlist (it's not an installed
# app), so this is how to get a screenshot of it.
#   powershell -File scripts\capture.ps1                       # main window
#   powershell -File scripts\capture.ps1 -Class SentinelSettingsDlg
param([string]$Class = "")
Add-Type -ReferencedAssemblies System.Drawing @"
using System; using System.Text; using System.Drawing; using System.Runtime.InteropServices;
public class Cap {
  delegate bool EnumProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc e, IntPtr p);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int m);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RC r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern int SetProcessDpiAwarenessContext(IntPtr v);
  [StructLayout(LayoutKind.Sequential)] public struct RC { public int L,T,R,B; }
  public static IntPtr ByClass(string cls) {
    IntPtr res = IntPtr.Zero;
    EnumWindows((h,p) => { if (IsWindowVisible(h)) { var c=new StringBuilder(128); GetClassName(h,c,128); if (c.ToString()==cls) res=h; } return true; }, IntPtr.Zero);
    return res;
  }
}
"@
[Cap]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null   # PER_MONITOR_AWARE_V2
$h = [IntPtr]::Zero
if ($Class -ne "") { $h = [Cap]::ByClass($Class) }
else { $p = Get-Process -Name Sentinel-IDE -EA SilentlyContinue | Select-Object -First 1; if ($p) { $h = $p.MainWindowHandle } }
if ($h -eq [IntPtr]::Zero) { Write-Output "no window (run the app first)"; exit 1 }
$r = New-Object Cap+RC; [Cap]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.R - $r.L; $hh = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap($w, $hh); $g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc(); [Cap]::PrintWindow($h, $dc, 2) | Out-Null; $g.ReleaseHdc($dc)
$out = Join-Path $PSScriptRoot "..\build\shot.png"
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose()
Write-Output "saved $out ($w x $hh)"
