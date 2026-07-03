# One-time Windows dev setup: persist the MSVC toolchain environment to the
# User environment so cl.exe, ninja.exe, and vcpkg resolve from any plain shell
# (PowerShell, Git Bash, IDEs) without a VS developer prompt.
#
# Idempotent: re-run after a Visual Studio / Build Tools update (the toolset
# version in the paths changes). Symptom of a stale environment:
#   fatal error C1034: cannot open include file: 'corecrt.h'
#
# What it persists (User scope): PATH additions for the MSVC compiler, the
# Windows SDK tools (rc/mt), and the VS-bundled Ninja; INCLUDE; LIB; VCPKG_ROOT.

$ErrorActionPreference = 'Stop'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found; install VS 2022 or VS Build Tools first." }

$vsRoot = & $vswhere -products * -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw "No Visual Studio installation with the C++ x64 toolset found." }
Write-Host "Visual Studio: $vsRoot"

# Run vcvars64 in a child cmd and capture the resulting environment.
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
$vcvarsEnv = @{}
cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
    $name, $value = $_ -split '=', 2
    if ($name -and $value) { $vcvarsEnv[$name] = $value }
}
if (-not $vcvarsEnv['INCLUDE']) { throw "vcvars64.bat did not produce an INCLUDE variable." }

# The three PATH entries a CMake+Ninja+MSVC build needs: cl/link, rc/mt, ninja.
# vcvars emits some entries with doubled backslashes (bin\10.0.x\\x64); normalize.
$vcvarsPath = $vcvarsEnv['Path'] -split ';' | ForEach-Object { ($_ -replace '\\+', '\').TrimEnd('\') }
$wanted = @()
$wanted += $vcvarsPath | Where-Object { $_ -match '\\VC\\Tools\\MSVC\\[^\\]+\\bin\\Hostx64\\x64$' } | Select-Object -First 1
$wanted += $vcvarsPath | Where-Object { $_ -match '\\Windows Kits\\10\\bin\\[^\\]+\\x64$' } | Select-Object -First 1
$ninjaDir = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
if (Test-Path (Join-Path $ninjaDir 'ninja.exe')) { $wanted += $ninjaDir }
$wanted = $wanted | Where-Object { $_ -and (Test-Path $_) }
if ($wanted.Count -lt 2) { throw "Could not locate the MSVC and Windows SDK bin directories in the vcvars PATH." }

# Rebuild the User PATH: drop entries this script manages (stale toolset
# versions from earlier runs), then append the current ones.
$managedPattern = '\\VC\\Tools\\MSVC\\|\\Windows Kits\\10\\bin\\|\\CommonExtensions\\Microsoft\\CMake\\Ninja'
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User') -split ';' |
    Where-Object { $_ -and $_ -notmatch $managedPattern }
$newPath = ($userPath + $wanted) -join ';'

[Environment]::SetEnvironmentVariable('Path', $newPath, 'User')
[Environment]::SetEnvironmentVariable('INCLUDE', $vcvarsEnv['INCLUDE'], 'User')
[Environment]::SetEnvironmentVariable('LIB', $vcvarsEnv['LIB'], 'User')
if (-not [Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'User')) {
    [Environment]::SetEnvironmentVariable('VCPKG_ROOT', 'C:\vcpkg', 'User')
}

Write-Host "Persisted to User environment:"
$wanted | ForEach-Object { Write-Host "  PATH += $_" }
Write-Host "  INCLUDE ($(($vcvarsEnv['INCLUDE'] -split ';').Count) dirs)"
Write-Host "  LIB ($(($vcvarsEnv['LIB'] -split ';').Count) dirs)"
Write-Host "  VCPKG_ROOT = $([Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'User'))"
Write-Host ""
Write-Host "Open a NEW shell for this to take effect, then verify: cl, ninja --version, ccache -V"
