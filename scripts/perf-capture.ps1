# Perf capture: drives the camera through the dev server and records /api/metrics.
# Requires a running world-sim instance in the game scene (default port 8081), which registers the
# camera and gpuhold controls. The dev server is in Debug and RelWithDebInfo builds; a Release build
# has none unless configured with -DSALAL_DEV_SERVER_IN_RELEASE=ON.
# X/Y pick the view (world meters); the scroll scenarios start from it.
#
# The game's GPU hold is on for the whole capture (docs/technical/ground-textures.md, "Clock state"):
# extra GPU work outside the scene's timers that keeps the card at P0, so the GPU times measure the
# code rather than the driver's clock choice. -NoGpuHold leaves it off. Each sample records the GPU's
# P-state and clocks from a read-only nvidia-smi query, and the script warns if any sample wasn't P0.
#
# Usage: pwsh scripts/perf-capture.ps1 [-Port 8081] [-X 256 -Y 256] [-SettleMs 1500] [-OutFile perf-results/capture.json]
#                                      [-NoGpuHold] [-GpuHoldTarget 0.8]

param(
    [int]$Port = 8081,
    [double]$X = 256,
    [double]$Y = 256,
    [int]$SettleMs = 1500,
    [string]$OutFile = "perf-results/capture-$(Get-Date -Format 'yyyy-MM-dd-HHmmss').json",
    [switch]$NoGpuHold,
    [double]$GpuHoldTarget = 0.8
)

$base = "http://127.0.0.1:$Port"
$smi = Get-Command nvidia-smi -ErrorAction SilentlyContinue

function Get-Metrics {
    Invoke-RestMethod "$base/api/metrics" -TimeoutSec 5
}

# The depth-sorted upright stream's draws (trees and actors), a share of drawCalls and triangles
function Get-Uprights {
    Invoke-RestMethod "$base/api/state?what=uprights" -TimeoutSec 5
}

function Get-GpuHold {
    if ($NoGpuHold) { return $null }
    Invoke-RestMethod "$base/api/state?what=gpuhold" -TimeoutSec 5
}

# P-state and clocks, read-only; nulls where nvidia-smi isn't installed
function Get-GpuClocks {
    if ($null -eq $smi) { return [pscustomobject]@{ pstate = $null; gpuClockMHz = $null; memClockMHz = $null } }
    $line = (& $smi.Source --query-gpu=pstate,clocks.gr,clocks.mem --format=csv,noheader | Select-Object -First 1)
    $parts = $line -split ',' | ForEach-Object { $_.Trim() }
    return [pscustomobject]@{
        pstate = $parts[0]
        gpuClockMHz = [int]($parts[1] -replace '[^0-9]', '')
        memClockMHz = [int]($parts[2] -replace '[^0-9]', '')
    }
}

function Set-Camera {
    param([string]$Query)
    Invoke-RestMethod "$base/api/control?action=camera&$Query" -TimeoutSec 5 | Out-Null
}

function Get-Median {
    param([double[]]$Values)
    $sorted = $Values | Sort-Object
    return $sorted[[int][math]::Floor($sorted.Count / 2)]
}

function Sample-Scenario {
    param([string]$Name, [int]$Samples = 9, [int]$IntervalMs = 600)
    $rows = @()
    foreach ($i in 1..$Samples) {
        Start-Sleep -Milliseconds $IntervalMs
        $m = Get-Metrics
        $hold = Get-GpuHold
        $up = Get-Uprights
        $clocks = Get-GpuClocks
        $rows += [pscustomobject]@{
            fps = $m.fps; frameTimeMs = $m.frameTimeMs; frameTimeMaxMs = $m.frameTimeMaxMs
            p99Ms = $m.frameTime1PercentLow; spikes16 = $m.spikeCount16ms; spikes33 = $m.spikeCount33ms
            drawCalls = $m.drawCalls; triangles = $m.triangleCount; vertices = $m.vertexCount
            uprightDrawCalls = $up.uprightDrawCalls; uprightTriangles = $up.uprightTriangles
            tileMs = $m.tileRenderMs; entityMs = $m.entityRenderMs; updateMs = $m.updateMs
            sceneUpdateMs = $m.sceneUpdateMs; sceneRenderMs = $m.sceneRenderMs; swapMs = $m.swapBuffersMs
            gpuMs = $m.gpuRenderMs; tileGpuMs = $m.tileGpuMs
            tileCount = $m.tileCount; entityCount = $m.entityCount; chunks = $m.visibleChunkCount
            pstate = $clocks.pstate; gpuClockMHz = $clocks.gpuClockMHz; memClockMHz = $clocks.memClockMHz
            holdPasses = $hold.passes; holdGpuMs = $hold.holdGpuMs
        }
    }
    $nonP0 = @($rows | Where-Object { $null -ne $_.pstate -and $_.pstate -ne 'P0' }).Count
    $clockText = if ($null -eq $smi) { 'pstate=?' } else {
        "P0 {0}/{1} gr={2}MHz" -f ($Samples - $nonP0), $Samples, (Get-Median $rows.gpuClockMHz)
    }
    Write-Host ("{0,-28} frame p50={1,6:F2}ms p99max={2,6:F2}ms gpu={3,6:F2}ms tileGpu={4,5:F2}ms tileMs={5,5:F2} entMs={6,5:F2} tris={7,9} upDraws={8} upTris={9,8} chunks={10} {11}" -f
        $Name, (Get-Median $rows.frameTimeMs), ($rows.p99Ms | Measure-Object -Maximum).Maximum, (Get-Median $rows.gpuMs),
        (Get-Median $rows.tileGpuMs), (Get-Median $rows.tileMs), (Get-Median $rows.entityMs),
        ($rows.triangles | Measure-Object -Maximum).Maximum, (Get-Median $rows.uprightDrawCalls), (Get-Median $rows.uprightTriangles),
        ($rows.chunks | Measure-Object -Maximum).Maximum, $clockText)
    return [pscustomobject]@{ scenario = $Name; gpuHold = (-not $NoGpuHold); nonP0Samples = $nonP0; samples = $rows }
}

$results = @()

if ($null -eq $smi) {
    Write-Host "nvidia-smi not found: the capture can't record or check the GPU's P-state" -ForegroundColor Yellow
}

try {
    # Disable vsync so frame times reflect real cost (the 120 FPS pacing cap still holds;
    # gpuMs and tileGpuMs show the cost under it)
    Invoke-RestMethod "$base/api/control?action=vsync&value=0" -TimeoutSec 5 | Out-Null
    if ($NoGpuHold) {
        Start-Sleep -Milliseconds 500
    } else {
        try {
            Invoke-RestMethod "$base/api/control?action=gpuhold&value=1&target=$GpuHoldTarget" -TimeoutSec 5 | Out-Null
        } catch {
            throw "The game has no gpuhold control (an older build?). Rebuild, or pass -NoGpuHold to capture without it. $_"
        }
        Start-Sleep -Milliseconds 2000  # the hold settles and the driver raises the clocks
    }

    # --- Scenario 1: idle at each zoom level ---
    Set-Camera "x=$X&y=$Y"
    foreach ($zoom in 20, 8, 3, 1.5, 0.75, 0.5, 0.25) {
        Set-Camera "zoom=$zoom"
        Start-Sleep -Milliseconds $SettleMs  # let chunk loads/bakes settle
        $results += Sample-Scenario -Name "idle zoom=$zoom"
    }

    # --- Scenario 2: scroll right at default zoom into fresh chunks ---
    Set-Camera "x=$X&y=$Y&zoom=3"
    Start-Sleep -Milliseconds 1000
    Set-Camera "panx=1"
    $results += Sample-Scenario -Name "scroll zoom=3" -Samples 25 -IntervalMs 400
    Set-Camera "panx=0&pany=0"

    # --- Scenario 3: scroll at zoomed-out 0.75 into fresh chunks (heavy tiles) ---
    Set-Camera "x=$X&y=$($Y - 2304)&zoom=0.75"
    Start-Sleep -Milliseconds 2000
    Set-Camera "pany=-1"
    $results += Sample-Scenario -Name "scroll zoom=0.75" -Samples 25 -IntervalMs 400
    Set-Camera "panx=0&pany=0"
} finally {
    # Restore defaults, even when the capture stops early
    if (-not $NoGpuHold) {
        try { Invoke-RestMethod "$base/api/control?action=gpuhold&value=0" -TimeoutSec 5 | Out-Null } catch {}
    }
    try {
        Invoke-RestMethod "$base/api/control?action=vsync&value=1" -TimeoutSec 5 | Out-Null
        Set-Camera "x=$X&y=$Y&zoom=3&panx=0&pany=0"
    } catch {}
}

$null = New-Item -ItemType Directory -Force (Split-Path $OutFile)
$results | ConvertTo-Json -Depth 5 | Set-Content $OutFile
Write-Host "Wrote $OutFile"

$offP0 = @($results | Where-Object { $_.nonP0Samples -gt 0 })
if ($offP0.Count -gt 0) {
    Write-Host ""
    Write-Host "WARNING: the GPU left P0 during this capture, so its GPU times measure the clock as well as the code:" -ForegroundColor Red
    foreach ($r in $offP0) {
        $states = ($r.samples | Where-Object { $_.pstate -ne 'P0' } | ForEach-Object { "$($_.pstate)@$($_.gpuClockMHz)MHz" }) -join ' '
        Write-Host ("  {0,-20} {1}/{2} samples off P0: {3}" -f $r.scenario, $r.nonP0Samples, $r.samples.Count, $states) -ForegroundColor Red
    }
    if ($NoGpuHold) {
        Write-Host "  Run without -NoGpuHold to hold the clocks." -ForegroundColor Red
    }
}
