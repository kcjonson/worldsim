# 2025-10-24 - Networking & Multiplayer Architecture

## Summary

Defined the complete client/server architecture for the game, including network protocol, synchronization strategies, and process management.

## Client/Server Architecture

**Two-process design from day one:**
- `world-sim` (client) + `world-sim-server` (headless)
- Server spawns only when needed (new game/load game), not during main menu
- Client manages server lifecycle: spawn, health monitoring, graceful shutdown
- Process management handles crash detection, zombie prevention, cross-platform spawning

## Network Protocol

| Layer | Protocol | Purpose |
|-------|----------|---------|
| Control Plane | HTTP (REST) | Create game, load game, chunk requests, health checks |
| Data Plane | WebSocket | 60 Hz entity updates, player input, world events |

**Message Format:** JSON initially, binary protocol (MessagePack/Protobuf) for future optimization

## Colony Sim Synchronization Strategy

Four object types with different sync strategies:

| Object Type | Update Frequency | Strategy |
|-------------|------------------|----------|
| Terrain | Rarely (mining/construction) | Chunk-based, heavy caching |
| Flora | Event-based (harvest, plague) | Batch updates for mass events |
| Structures | Delta updates | Property changes only |
| Entities | 60 Hz | Full state via WebSocket |

## Library Choice

**cpp-httplib** selected for both game server and debug server:
- Header-only, simple integration
- SSE support for debug streaming
- WebSocket support for gameplay

## Technical Documentation Created

| File | Description |
|------|-------------|
| `/docs/design/features/debug-server/README.md` | Debug server game design doc |
| `/docs/design/features/multiplayer/README.md` | Multiplayer architecture game design doc |
| `/docs/technical/http-debug-server.md` | Debug server implementation design |
| `/docs/technical/multiplayer-architecture.md` | Client/server protocol and synchronization |
| `/docs/technical/process-management.md` | Cross-platform process lifecycle management |

## Key Technical Decisions

- **cpp-httplib** chosen for both game server and debug server
- Server-Sent Events preferred over WebSocket for debug server (one-way streaming, simpler, auto-reconnect)
- WebSocket required for game server (bidirectional, low-latency gameplay)
- Process spawning: fork/exec on Unix, CreateProcess on Windows
- Health monitoring: HTTP polling every 5s + PID checks for crash detection

## Library Organization Clarified

| Library | Scope | Notes |
|---------|-------|-------|
| `game-systems` | SERVER ONLY | Colony simulation logic, must not depend on renderer |
| `renderer`, `ui` | CLIENT ONLY | Never linked by server |
| `world` | SHARED | Procedural generation algorithms |
| `engine/shared` | SHARED | Protocol, message types |
| `engine/client` | CLIENT ONLY | Prediction, interpolation |
| `engine/server` | SERVER ONLY | Validation, broadcasting |

## Related Documentation

- [Multiplayer Architecture](/docs/technical/multiplayer-architecture.md)
- [Process Management](/docs/technical/process-management.md)

## Next Steps (at time of writing)

1. Begin implementing process management (client spawning server)
2. Set up cpp-httplib in vcpkg.json
3. Create basic HTTP server skeleton in `world-sim-server`
4. Implement health check endpoint
