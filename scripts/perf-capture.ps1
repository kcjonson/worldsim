# Perf capture: drives the camera via the debug server and records /api/metrics.
# Requires a running world-sim instance in the game scene (default port 8081).
# X/Y pick the view (world meters); the scroll scenarios start from it.
#
# Usage: pwsh scripts/perf-capture.ps1 [-Port 8081] [-X 256 -Y 256] [-SettleMs 1500] [-OutFile perf-results/capture.json]

param(
    [int]$Port = 8081,
    [double]$X = 256,
    [double]$Y = 256,
    [int]$SettleMs = 1500,
    [string]$OutFile = "perf-results/capture-$(Get-Date -Format 'yyyy-MM-dd-HHmmss').json"
)

$base = "http://127.0.0.1:$Port"

function Get-Metrics {
    Invoke-RestMethod "$base/api/metrics" -TimeoutSec 5
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
        $rows += [pscustomobject]@{
            fps = $m.fps; frameTimeMs = $m.frameTimeMs; frameTimeMaxMs = $m.frameTimeMaxMs
            p99Ms = $m.frameTime1PercentLow; spikes16 = $m.spikeCount16ms; spikes33 = $m.spikeCount33ms
            drawCalls = $m.drawCalls; triangles = $m.triangleCount; vertices = $m.vertexCount
            tileMs = $m.tileRenderMs; entityMs = $m.entityRenderMs; updateMs = $m.updateMs
            sceneUpdateMs = $m.sceneUpdateMs; sceneRenderMs = $m.sceneRenderMs; swapMs = $m.swapBuffersMs
            gpuMs = $m.gpuRenderMs; tileGpuMs = $m.tileGpuMs
            tileCount = $m.tileCount; entityCount = $m.entityCount; chunks = $m.visibleChunkCount
        }
    }
    Write-Host ("{0,-28} frame p50={1,6:F2}ms p99max={2,6:F2}ms gpu={3,6:F2}ms tileGpu={4,5:F2}ms tileMs={5,5:F2} entMs={6,5:F2} tris={7,9} chunks={8}" -f
        $Name, (Get-Median $rows.frameTimeMs), ($rows.p99Ms | Measure-Object -Maximum).Maximum, (Get-Median $rows.gpuMs),
        (Get-Median $rows.tileGpuMs), (Get-Median $rows.tileMs), (Get-Median $rows.entityMs),
        ($rows.triangles | Measure-Object -Maximum).Maximum, ($rows.chunks | Measure-Object -Maximum).Maximum)
    return [pscustomobject]@{ scenario = $Name; samples = $rows }
}

$results = @()

# Disable vsync so frame times reflect real cost (the 120 FPS pacing cap still holds;
# gpuMs and tileGpuMs show the cost under it)
Invoke-RestMethod "$base/api/control?action=vsync&value=0" -TimeoutSec 5 | Out-Null
Start-Sleep -Milliseconds 500

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

# Restore defaults
Invoke-RestMethod "$base/api/control?action=vsync&value=1" -TimeoutSec 5 | Out-Null
Set-Camera "x=$X&y=$Y&zoom=3"

$null = New-Item -ItemType Directory -Force (Split-Path $OutFile)
$results | ConvertTo-Json -Depth 5 | Set-Content $OutFile
Write-Host "Wrote $OutFile"
