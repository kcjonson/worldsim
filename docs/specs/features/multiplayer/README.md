# Multiplayer Architecture

## Overview

The game is built from the ground up with a backend/client architecture. Even single-player mode runs as two separate processes: a game server (backend) and a game client, communicating over HTTP/WebSocket on localhost. This design enables seamless scaling from single-player to local multiplayer to online multiplayer without architectural rewrites.

## Problem Statement

Retrofitting multiplayer into a single-player game is notoriously difficult:
- Tight coupling between game logic and rendering
- Lack of authoritative server validation
- Difficult to add prediction and lag compensation later
- Client-side state management conflicts with server authority

## Solution

Build the separation from day one with two distinct executables:
1. **Game Server (Backend)**: Authoritative game state, logic, world generation
2. **Game Client**: Rendering, input, local prediction, interpolation
3. **Communication**: HTTP + WebSocket protocol for state synchronization

All game modes use this architecture, even single-player.

### Phased Rollout

#### Phase 1: Local Single-Player (Initial Release)
- Backend and client run as separate processes on same machine
- Client spawns and manages server as child process
- Single player per server instance
- Localhost-only communication
- Proves architecture works, establishes clean separation

#### Phase 2: Local Multiplayer
- Server runs as daemon (detached from client)
- Multiple clients connect to same local backend
- Shared world state between players
- LAN play possible (backend binds to local network IP)

#### Phase 3: Online Multiplayer
- Backend runs on remote dedicated server
- Authentication and player accounts
- Matchmaking, server browser
- NAT traversal or relay servers

## Architecture: Backend/Client Separation

### Backend Responsibilities (Game Server)

**Core Responsibilities:**
- Maintain authoritative game state (entities, world chunks, player positions)
- Execute game logic at fixed tick rate (60 TPS)
- Handle world generation and chunk streaming
- Validate all player actions (movement, interactions, building)
- Broadcast state updates to connected clients
- Persist game state (save/load)
- Run headless (no graphics, no window)

**Backend does NOT:**
- Render anything (headless process)
- Handle input directly (receives input events from clients)
- Make client-side predictions
- Store client-specific UI state

**Backend Implementation:**
- Separate executable: `world-sim-server` (or `world-sim-server.exe`)
- Uses libraries: `foundation`, `world`, `game-systems`, `engine/server`
- Does NOT link against `renderer` or `engine/client`

### Client Responsibilities (Game Renderer)

**Core Responsibilities:**
- Render the game world (2D tiles, UI, effects, camera)
- Capture player input and send to server
- **Spawn and monitor server process** (Phase 1)
- Predict local player movement (client-side prediction)
- Interpolate remote entity positions for smooth rendering
- Handle camera controls and viewport
- Display UI (menus, HUD, inventory)
- Play audio and visual effects
- Cache world chunks locally

**Client does NOT:**
- Make authoritative decisions about game state
- Generate world data (requests chunks from server)
- Validate player actions (server's job)
- Run game logic (only renders server state)

**Client Implementation:**
- Separate executable: `world-sim` (main game executable)
- Uses libraries: `foundation`, `renderer`, `ui`, `engine/client`
- Does NOT link against `game-systems` (no game logic!)

## Process Management (Phase 1)

### Client-Managed Server Lifecycle

**Startup Sequence:**
1. User launches `world-sim` (client executable)
2. Client checks if server is running: `GET http://localhost:9000/api/health`
3. If server not responding:
   - Client spawns `world-sim-server` as child process
   - Wait for health check to succeed (retry 10x with 500ms delay)
   - If fails: Show error "Server failed to start, check logs"
4. Client sends `POST /api/game/create` to create new game
5. Client receives game ID and player authentication token
6. Client connects WebSocket: `ws://localhost:9000/ws/{token}`
7. Game begins

**Monitoring:**
- Client polls `GET /api/health` every 5 seconds (background thread)
- If health check fails AND process is dead → Server crashed
- Show error dialog: "Server crashed. [Restart] [View Logs] [Quit]"

**Shutdown Sequence:**
1. User quits game
2. Client sends `POST /api/shutdown` to server
3. Server: Save world → Close WebSocket → Exit cleanly
4. Client waits up to 5 seconds for server process to exit
5. If server still running: Force kill server process
6. Client reaps zombie process (Unix: `waitpid()`)
7. Client exits

### Process Monitoring Details

**Health Check vs PID Check:**
- **HTTP health check**: Detects server alive AND responsive
- **PID check**: Detects server crashed (process dead)
- Use both: Health check fails → Check PID → Determine if crashed or hung

**Zombie Prevention:**
```
Unix/Linux/macOS:
  - Install SIGCHLD handler to reap dead children
  - Or explicitly waitpid() on shutdown

Windows:
  - No zombies, but must CloseHandle() on process handle
```

## Communication Protocol

### Transport Layer: HTTP + WebSocket

**HTTP (REST) - Initial Connection & Control**
```
GET  /api/health             → Health check (returns {"status": "ok", "uptime": 1234})
POST /api/game/create        → Create new game, receive game ID
POST /api/game/join/{id}     → Join existing game, receive player ID + token
GET  /api/game/state         → Initial state snapshot (on connect)
GET  /api/world/chunk/{x}/{y} → Request world chunk data
POST /api/game/save          → Trigger server-side save
POST /api/shutdown           → Graceful server shutdown
```

**WebSocket - Real-time Bidirectional Streaming**
```
ws://localhost:9000/ws/{playerToken}

Client → Server (60 Hz):
  - Input events (movement, actions)
  - Action requests (mine tile, place structure)
  - Chunk requests (as viewport moves)

Server → Client (60 Hz):
  - Entity state updates (positions, health, animations)
  - World events (tile changes, entity spawns/deaths)
  - Game events (time progression, weather)
  - Acknowledgements (confirm input received)
```

**Why HTTP for Control, WebSocket for Game Data?**
- HTTP: Request/response pattern fits control operations (create game, shutdown)
- WebSocket: Low-latency bidirectional streaming for real-time game state
- HTTP health checks work even if WebSocket is broken

### Message Format

All messages are JSON (initially). Binary protocol (MessagePack/Protobuf) can be added later for optimization.

**Client Input Message:**
```json
{
  "type": "input",
  "timestamp": 1729800000,
  "sequence": 12345,
  "movement": {"x": 0.5, "y": 0.0},
  "actions": ["mine_tile"],
  "camera": {"x": 100, "y": 200, "zoom": 1.0}
}
```

**Server State Update:**
```json
{
  "type": "state_update",
  "timestamp": 1729800001,
  "tick": 54321,
  "entities": [
    {"id": 42, "x": 100.5, "y": 200.3, "sprite": "player", "animation": "walk"},
    {"id": 43, "x": 105.2, "y": 198.7, "sprite": "tree", "health": 100}
  ],
  "ack_sequence": 12345
}
```

## Deployment Structure

### macOS (.app Bundle)
```
WorldSim.app/
  Contents/
    MacOS/
      world-sim          ← Client executable (spawns server)
      world-sim-server   ← Server executable (headless)
    Resources/
      assets/
```

### Windows
```
WorldSim/
  world-sim.exe          ← Client executable
  world-sim-server.exe   ← Server executable
  assets/
```

### Linux
```
/opt/world-sim/
  bin/
    world-sim            ← Client executable
    world-sim-server     ← Server executable
  share/world-sim/
    assets/
```

**Process Manager View (normal operation):**
```
USER       PID   COMMAND
player    1234   world-sim (parent)
player    1235   └─ world-sim-server --port 9000 (child)
```

## Requirements

### Functional Requirements

#### Process Management (Phase 1)
- **REQ-PM-001**: Client spawns server as child process on startup
- **REQ-PM-002**: Client monitors server health via HTTP polling (5s interval)
- **REQ-PM-003**: Client detects server crashes (HTTP fail + dead PID)
- **REQ-PM-004**: Client performs graceful shutdown of server on exit
- **REQ-PM-005**: Client force-kills server if graceful shutdown times out (5s)
- **REQ-PM-006**: Client reaps zombie processes (Unix platforms)
- **REQ-PM-007**: Server logs to file in user directory (`~/.world-sim/server.log`)

#### Backend (Game Server)
- **REQ-MP-001**: Runs as standalone headless executable (`world-sim-server`)
- **REQ-MP-002**: Listens on configurable port (default: 9000, localhost only)
- **REQ-MP-003**: Supports multiple client connections (Phase 2: 10-50 players)
- **REQ-MP-004**: Maintains authoritative game state in ECS
- **REQ-MP-005**: Ticks game logic at fixed rate (60 TPS)
- **REQ-MP-006**: Generates and streams world chunks on-demand
- **REQ-MP-007**: Validates all player actions before applying
- **REQ-MP-008**: Broadcasts entity state updates to clients (60 Hz)
- **REQ-MP-009**: Handles client disconnections gracefully (timeout after 10s)
- **REQ-MP-010**: Persists world state to disk (autosave every 5 minutes)
- **REQ-MP-011**: Responds to `/api/health` endpoint (uptime, status)
- **REQ-MP-012**: Responds to `/api/shutdown` endpoint (graceful exit)

#### Client (Game Renderer)
- **REQ-MP-013**: Connects to backend via WebSocket (configurable host:port)
- **REQ-MP-014**: Sends player input at 60 Hz (every rendered frame)
- **REQ-MP-015**: Receives and applies state updates from server (60 Hz)
- **REQ-MP-016**: Implements client-side prediction for local player movement
- **REQ-MP-017**: Interpolates remote entity positions for smooth rendering
- **REQ-MP-018**: Handles server disconnection (show error, restart or quit)
- **REQ-MP-019**: Displays network stats in debug mode (latency, tick rate)
- **REQ-MP-020**: Caches world chunks locally (LRU eviction, max 1000 chunks)
- **REQ-MP-021**: Shows connection status indicator

#### Communication Protocol
- **REQ-MP-022**: WebSocket connection with JSON messages
- **REQ-MP-023**: Message versioning for protocol evolution
- **REQ-MP-024**: Heartbeat/ping every 5 seconds (detect dead connections)
- **REQ-MP-025**: Sequence numbers on client inputs (detect reordering/loss)
- **REQ-MP-026**: Server acknowledges client inputs (confirm processing)
- **REQ-MP-027**: Gzip compression for large messages (chunk data >10 KB)

### Non-Functional Requirements

#### Performance
- **REQ-MP-NF-001**: Backend tick rate: 60 TPS (16.6ms per tick budget)
- **REQ-MP-NF-002**: Client renders at 60 FPS minimum
- **REQ-MP-NF-003**: Localhost latency: <1ms (Phase 1)
- **REQ-MP-NF-004**: State update bandwidth: <100 KB/s per client
- **REQ-MP-NF-005**: Chunk loading latency: <200ms from request to display

#### Reliability
- **REQ-MP-NF-006**: Graceful degradation on packet loss (<5% tolerable)
- **REQ-MP-NF-007**: Server crash recovery (load last autosave)
- **REQ-MP-NF-008**: No zombie processes left after client exit
- **REQ-MP-NF-009**: Server startup time <2 seconds

## Related Documentation

- [Technical Design: Multiplayer Architecture](../../../technical/multiplayer-architecture.md)
- [Technical Design: Process Management](../../../technical/process-management.md)
- [Technical Design: ECS Design](../../../technical/cpp-coding-standards.md#ecs)
- [HTTP Debug Server](../debug-server/README.md) (similar WebSocket implementation)
