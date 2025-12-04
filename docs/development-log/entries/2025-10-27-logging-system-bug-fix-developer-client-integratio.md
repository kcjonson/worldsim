# Logging System Bug Fix - Developer Client Integration

**Date:** 2025-10-27

**Critical Bug Fixed:**

Discovered and fixed two bugs preventing DEBUG logs from appearing in the developer client browser UI.

**Bug #1: Pre-Filtering Issue**

**Problem:** Logger was filtering logs by level BEFORE sending to debug server. This meant DEBUG logs (and any filtered logs) never reached the developer client, even though the client has its own filtering UI.

**Root Cause:** In `Logger::Log()` (`libs/foundation/utils/log.cpp`), the level filter check happened before calling `debugServer->UpdateLog()`:
```cpp
// OLD CODE (WRONG):
if (level < GetLevel(category)) {
    return;  // Too verbose, skip - NEVER REACHES DEBUG SERVER!
}
// ... format message ...
if (s_debugServer) {
    s_debugServer->UpdateLog(...);  // DEBUG logs never get here
}
```

**Fix:** Reordered code to send ALL logs to debug server before applying console filtering:
```cpp
// NEW CODE (CORRECT):
// Format message first
char message[256];
vsnprintf(message, sizeof(message), format, args);

// Send to debug server (ALL logs, regardless of console filter)
if (s_debugServer) {
    s_debugServer->UpdateLog(...);
}

// THEN apply level filter for console output
if (level < GetLevel(category)) {
    return;  // Skip console, but already sent to debug server
}
```

**Result:** Developer client receives ALL logs, users can filter in browser UI. Console still respects level filters (less noise).

**Bug #2: Race Condition on Startup**

**Problem:** Debug server was initialized AFTER most startup logs fired, so early logs never reached the ring buffer (`s_debugServer` was nullptr).

**Fix:** Moved debug server initialization to very beginning of `main()`:
```cpp
int main() {
    // Parse args FIRST (no logging yet)
    // ...

    Logger::Initialize();

    // Start debug server IMMEDIATELY (before any logs)
    Foundation::DebugServer debugServer;
    foundation::Logger::SetDebugServer(&debugServer);
    debugServer.Start(8081);

    // NOW all logs go to ring buffer
    LOG_INFO(UI, "UI Sandbox - Component Testing & Demo Environment");
    // ...
}
```

**Result:** All startup logs (including early DEBUG logs) are captured in ring buffer and available when client connects.

**Bug #3: Same-Timestamp Log Dropping**

**Problem:** When multiple logs fired within the same millisecond (common during startup), only the first log was sent to clients. Subsequent logs with the same timestamp were dropped.

**Root Cause:** Debug server used `>` (greater than) for timestamp comparison:
```cpp
// OLD CODE (WRONG):
if (entry.timestamp > lastSentTimestamp) {
    // Send log
    lastSentTimestamp = entry.timestamp;
}
// Logs with same timestamp as lastSent are DROPPED but already consumed from ring buffer!
```

**Fix:** Changed to `>=` to handle multiple logs with same timestamp:
```cpp
// NEW CODE (CORRECT):
if (entry.timestamp >= lastSentTimestamp) {
    // Send log
    lastSentTimestamp = entry.timestamp;
}
```

**Result:** All logs sent to client, even when multiple fire in the same millisecond.

**Performance Impact:**
- Debug server initialization: ~1ms (one-time at startup)
- Message formatting: ~100-500ns per log (acceptable for DEVELOPMENT_BUILD only)
- Lock-free ring buffer writes: ~10-20ns (unchanged)
- **Total impact: Negligible** (~1ms startup delay, sub-microsecond per log)

**Testing:**
- Verified DEBUG logs from all categories appear in developer client
- Verified console still filters logs by level (UI category set to Info, DEBUG logs hidden in console but visible in browser)
- Verified early startup logs appear in browser
- Verified repeating logs (frame counter) stream correctly
- Verified all INFO logs appear (no more missing logs due to timestamp collision)

**Files Modified:**
- `libs/foundation/utils/log.cpp` - Reordered Logger::Log() to send to debug server before filtering
- `libs/foundation/debug/debug_server.cpp` - Fixed timestamp comparison (> to >=)
- `apps/ui-sandbox/main.cpp` - Moved debug server initialization to very beginning

**Impact:**
Complete observability for development - ALL logs now stream to developer client regardless of console filtering, enabling full debugging without console noise.


