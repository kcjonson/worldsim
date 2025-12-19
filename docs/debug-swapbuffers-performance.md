# Debug Log: swapBuffers Performance

**Issue:** glfwSwapBuffers takes 40-50ms for a simple 2D game with only 13K triangles
**Started:** 2025-12-19
**Status:** INVESTIGATING

---

## Problem Summary

**Observed:**
- FPS: ~21 (frame time ~47ms)
- swapBuffers: 40-50ms (92% of frame time)
- Scene render: 4-6ms (CPU time to issue draw calls)

**Expected:**
- FPS: 60+ (frame time <16.67ms)
- swapBuffers: <1ms (for trivial GPU workload)

**Key metrics:**
- Draw calls: 4
- Triangles: 13K
- Vertices: 27K
- Entity count: 691K (cached flora data, not per-frame work)
- CPU usage: 10-15%
- GPU timer: 0.00ms (not returning valid data)

---

## Key Facts Established

1. Scene render (CPU time to issue GL commands) is only 4-6ms - fast
2. swapBuffers blocks for 40-50ms - this is the bottleneck
3. GPU timer queries are "supported" (ARB=1, GL3.3=1) but return 0.00ms
4. VSync was disabled (glfwSwapInterval(0)) but made no difference
5. 40-50ms does NOT match standard refresh rates (60Hz=16.67ms, 120Hz=8.33ms)
6. ECS systems all run in <0.01ms - not the issue
7. Only 4 draw calls suggests good batching

---

## Scout Agent Findings

**Explored by:** 3 parallel agents
**Total hypotheses generated:** TBD
**After deduplication:** TBD

---

## Hypotheses (Ranked)

TBD - awaiting scout agent results

---

## Investigation Log

### Session 1 (2025-12-19)
**Goal:** Identify root cause of swapBuffers delay
**Prior work (before formal debug session):**
- Enabled GPU timer in GameScene
- Disabled VSync (no improvement)
- Added GPU timer logging (queries supported but returning 0)

**Debug code added:**
- libs/renderer/metrics/GPUTimer.cpp - logging for timer support
- libs/engine/application/AppLauncher.cpp - VSync disabled (glfwSwapInterval(0))
- apps/world-sim/scenes/GameScene.cpp - m_gpuTimer.setEnabled(true)

**Next:** Launch scout agents for hypothesis generation
