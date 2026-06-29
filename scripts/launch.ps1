# Launch SentinelIDE detached (survives this shell), optionally with args.
# Detached via WMI so the IDE keeps running after the script returns — needed
# because the build/run shell otherwise kills child processes on exit.
#   powershell -File scripts\launch.ps1 "G:\SentinelIDE\examples"
#   powershell -File scripts\launch.ps1 "G:\SentinelIDE\examples" --build --tier hardened
#   powershell -File scripts\launch.ps1 "G:\SentinelIDE\examples" --project-settings
# (Don't name the param $Args — that collides with PowerShell's automatic $Args and
# silently drops everything; collect all positional args via ValueFromRemainingArguments.)
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest)
Get-Process -Name Sentinel-IDE -EA SilentlyContinue | Stop-Process -Force -EA SilentlyContinue
Start-Sleep -Milliseconds 400
$exe = Join-Path $PSScriptRoot "..\build\Sentinel-IDE.exe"
$cl = '"' + (Resolve-Path $exe).Path + '"'
if ($Rest) { $cl += " " + (($Rest | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join " ") }
$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $cl }
Write-Output ("launched pid=" + $r.ProcessId + "  rc=" + $r.ReturnValue)
