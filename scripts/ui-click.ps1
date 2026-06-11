# Dev helper: click at logical UI coordinates inside the running world-sim window.
# Usage: ui-click.ps1 -X 1536 -Y 869 [-Right]
# Verifies world-sim owns the foreground window before synthesizing input; exits 2 otherwise.
param(
	[Parameter(Mandatory)] [int]$X,
	[Parameter(Mandatory)] [int]$Y,
	[switch]$Right
)

$ErrorActionPreference = 'Stop'

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class WinInput {
	[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
	[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
	[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
	[DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT pt);
	[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
	[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
	[DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
	public struct POINT { public int X; public int Y; }
	public const uint LEFTDOWN = 0x02, LEFTUP = 0x04, RIGHTDOWN = 0x08, RIGHTUP = 0x10;
}
'@

[WinInput]::SetProcessDPIAware() | Out-Null

$proc = Get-Process world-sim -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $proc) { Write-Output "no world-sim window"; exit 2 }
$hwnd = $proc.MainWindowHandle

[WinInput]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 150

$fg = [WinInput]::GetForegroundWindow()
$fgPid = 0
[WinInput]::GetWindowThreadProcessId($fg, [ref]$fgPid) | Out-Null
if ($fgPid -ne $proc.Id) { Write-Output "world-sim not foreground (pid $fgPid is)"; exit 2 }

$pt = New-Object WinInput+POINT
$pt.X = $X; $pt.Y = $Y
[WinInput]::ClientToScreen($hwnd, [ref]$pt) | Out-Null
[WinInput]::SetCursorPos($pt.X, $pt.Y) | Out-Null
Start-Sleep -Milliseconds 60

if ($Right) {
	[WinInput]::mouse_event([WinInput]::RIGHTDOWN, 0, 0, 0, [UIntPtr]::Zero)
	Start-Sleep -Milliseconds 40
	[WinInput]::mouse_event([WinInput]::RIGHTUP, 0, 0, 0, [UIntPtr]::Zero)
} else {
	[WinInput]::mouse_event([WinInput]::LEFTDOWN, 0, 0, 0, [UIntPtr]::Zero)
	Start-Sleep -Milliseconds 40
	[WinInput]::mouse_event([WinInput]::LEFTUP, 0, 0, 0, [UIntPtr]::Zero)
}
Write-Output "clicked $X,$Y"
