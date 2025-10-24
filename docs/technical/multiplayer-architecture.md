# Multiplayer Architecture - Technical Design

## Overview

The game uses a client/server architecture from day one. Even single-player mode runs as two separate processes: `world-sim-server` (backend) and `world-sim` (client), communicating over HTTP/WebSocket on localhost. This design enables seamless scaling from single-player to multiplayer without architectural rewrites.

**Game Type**: Colony simulation (Rimworld-like) with vector-based (SVG) assets, procedural world generation, and emergent storytelling through simulation systems.

## Core Architecture

### Two-Process Design

```
┌──────────────────────────────────┐
│ world-sim (Client Process)       │
│ - Rendering (60 FPS)             │
│ - Input capture                  │
│ - SVG rasterization + caching    │
│ - Client-side prediction         │
│ - Entity interpolation           │
│ - Spawns/monitors server         │
└─────────┬────────────────────────┘
          │ WebSocket
          │ localhost:9000
          │ 60 Hz bidirectional
┌─────────▼────────────────────────┐
│ world-sim-server (Server Process)│
│ - Game logic (60 TPS)            │
│ - Authoritative state (ECS)      │
│ - World generation               │
│ - Input validation               │
│ - Colony simulation              │
│ - Headless (no rendering)        │
└──────────────────────────────────┘
```

**Phase 1 (Current):**
- Client spawns server as child process
- Single player per server
- Localhost-only communication
- Client manages server lifecycle

**Phase 2 (Future):**
- Server runs as daemon
- Multiple clients connect (local multiplayer)
- LAN play

**Phase 3 (Future):**
- Dedicated remote servers
- Authentication, matchmaking
- Online multiplayer

## World Object Taxonomy

The game world consists of four distinct object categories, each with different synchronization strategies:

### 1. Terrain (Base Layer)

**Characteristics:**
- Tile-based ground: grass, dirt, stone, water
- Changes rarely (player mining/construction)
- Fixed grid positions
- Foundation for all other objects

**Synchronization:**
- Sent once per chunk on initial load
- Delta updates when tiles change (mining, construction)
- Heavy client-side caching (LRU, max 1000 chunks)

### 2. Flora (Vegetation Layer)

**Characteristics:**
- Plants, trees, bushes, crops
- Positioned on terrain (not tile-locked)
- Dynamic: grow, die, affected by events (plagues, fires)
- Properties: health, growth stage, species type

**Synchronization:**
- Initial state sent with chunk data
- Individual updates when manually interacted with (harvested, planted)
- Batch updates for mass events (plague, fire, meteor)
- No tick-by-tick updates (growth happens server-side, visible changes are discrete)

### 3. Structures (Built/Static Layer)

**Characteristics:**
- **Buildings**: Player-constructed (workshops, storage, walls)
- **Ruins**: Pre-existing world features (abandoned structures, monuments)
- **Natural Formations**: Rock outcroppings, geysers
- All use vector assets (SVG), not tiles
- Properties: position, rotation, health, construction progress

**Synchronization:**
- Initial state sent with chunk data
- Event-based updates (placed, damaged, destroyed)
- Delta updates for changing properties (construction progress, health)
- Client renders from local SVG assets + server metadata

### 4. Entities (Dynamic Layer)

**Characteristics:**
- Colonists, animals, raiders
- Items, dropped resources
- Constantly moving/changing
- Full AI/behavior on server

**Synchronization:**
- State updates every tick (60 Hz) via WebSocket
- Client-side interpolation for smooth movement
- Only entities in visible area sent to clients

## Library Organization

### Strict Dependency Separation

```
apps/
  world-sim/              ← Client executable
    CMakeLists.txt:
      links: foundation, renderer, ui, engine/client
      NOT: game-systems (no game logic in client!)

  world-sim-server/       ← Server executable
    CMakeLists.txt:
      links: foundation, world, game-systems, engine/server
      NOT: renderer, ui (headless!)

libs/
  foundation/             → SHARED (math, strings, memory, logging)
  renderer/               → CLIENT ONLY (OpenGL, textures, SVG rasterization)
  ui/                     → CLIENT ONLY (UI components)
  world/                  → SHARED (world generation algorithms)
  game-systems/           → SERVER ONLY (colony sim logic, ECS systems, AI)
  engine/
    shared/               → SHARED (protocol, message types)
    client/               → CLIENT ONLY (prediction, interpolation)
    server/               → SERVER ONLY (validation, broadcasting)
```

**Critical Rule**: `game-systems` MUST NOT depend on `renderer` or `ui`. Colony simulation is headless and authoritative.

### CMake Enforcement

```cmake
# apps/world-sim/CMakeLists.txt (Client)
add_executable(world-sim main.cpp)
target_link_libraries(world-sim PRIVATE
    foundation
    renderer
    ui
    engine-client
    # NOTE: game-systems intentionally excluded!
)

# apps/world-sim-server/CMakeLists.txt (Server)
add_executable(world-sim-server main.cpp)
target_link_libraries(world-sim-server PRIVATE
    foundation
    world
    game-systems
    engine-server
    # NOTE: renderer and ui intentionally excluded!
)
```

## Communication Protocol

### Transport: HTTP + WebSocket

**HTTP (REST) - Connection Setup & Control**

```
POST /api/game/create
  Request:  {"worldSeed": 12345, "colonyName": "New Hope"}
  Response: {"gameId": "abc123", "playerToken": "xyz789"}

POST /api/game/join/{gameId}
  Request:  {"playerName": "Player2", "password": "optional"}
  Response: {"playerToken": "def456", "playerId": 2}

GET /api/game/state
  Headers:  Authorization: Bearer {playerToken}
  Response: {colony position, initial chunks, colonist data}

GET /api/world/chunk/{x}/{y}
  Headers:  Authorization: Bearer {playerToken}
  Response: {terrain tiles, flora, structures, entities in chunk}

GET /api/health
  Response: {"status": "ok", "uptime": 1234, "players": 1}

POST /api/shutdown
  Response: {"status": "shutting down"}
```

**WebSocket - Real-Time Game State**

```
Connection: ws://localhost:9000/ws/{playerToken}

Client → Server (60 Hz):
  - Player input (selection, orders, construction)
  - Camera position (for area-of-interest filtering)

Server → Client (60 Hz):
  - Entity state updates (colonists, animals moving)
  - World events (fires, births, deaths)
  - Batch updates (plagues, construction progress)
  - Input acknowledgments
```

**Why HTTP + WebSocket?**
- **HTTP**: Request/response for stateful operations (create game, get chunks)
- **WebSocket**: Low-latency streaming for real-time colony simulation
- **Separation**: Control plane vs data plane

### Message Format (JSON)

All messages are JSON initially. Binary protocol (MessagePack/Protobuf) can be added later.

#### Client Input Message

```json
{
  "type": "input",
  "timestamp": 1729800000,
  "sequence": 12345,
  "actions": [
    {
      "action": "order_construct",
      "buildingType": "workshop",
      "x": 50.5,
      "y": 70.2,
      "rotation": 90
    },
    {
      "action": "order_harvest",
      "targets": [5001, 5002, 5003]
    }
  ]
}
```

#### Server State Update (Every Tick)

```json
{
  "type": "state_update",
  "timestamp": 1729800001,
  "tick": 54321,
  "entities": [
    {
      "id": 100,
      "type": "colonist",
      "x": 45.2,
      "y": 67.8,
      "velocity": {"x": 0.5, "y": 0.0},
      "animation": "walk",
      "carrying": "wood"
    }
  ],
  "structureUpdates": [
    {
      "id": 1001,
      "constructionProgress": 0.76
    }
  ],
  "ackSequence": 12345
}
```

#### Chunk Data (On-Demand via HTTP)

```json
{
  "type": "chunk_data",
  "chunkX": 5,
  "chunkY": 5,
  "version": 12,
  "terrain": [
    {"x": 0, "y": 0, "type": "grass", "variant": 3},
    {"x": 1, "y": 0, "type": "dirt", "variant": 1}
  ],
  "flora": [
    {
      "id": 5001,
      "type": "oak_tree",
      "x": 12.5,
      "y": 15.3,
      "growth": 0.8,
      "health": 100
    }
  ],
  "structures": [
    {
      "id": 1001,
      "type": "storage_shed",
      "x": 45.5,
      "y": 67.2,
      "rotation": 90,
      "constructionProgress": 1.0,
      "health": 100,
      "variant": 2
    }
  ],
  "entities": [100, 101, 102]
}
```

#### Batch Update (Mass Events)

```json
{
  "type": "batch_update",
  "updateType": "plant_plague",
  "timestamp": 1729800000,
  "affectedObjects": {
    "targetType": "id_list",
    "objectType": "flora",
    "ids": [5001, 5002, 5003, 5004, 5005],
    "changes": {
      "health": 0,
      "alive": false
    }
  }
}
```

**Batch Target Type Options:**

```json
// 1. Explicit ID list (small batches)
{
  "targetType": "id_list",
  "ids": [1, 2, 3, 4, 5]
}

// 2. ID range (contiguous IDs, efficient)
{
  "targetType": "id_range",
  "minId": 5000,
  "maxId": 6247
}

// 3. Area query (server calculates, client re-requests chunks)
{
  "targetType": "area",
  "bounds": {"x": 0, "y": 0, "width": 100, "height": 100},
  "affectedCount": 1247
}
```

**Batch Update Strategies by Event Type:**

| Event Type | Typical Count | Target Strategy | Example |
|------------|---------------|-----------------|---------|
| `building_placed` | 1 | Single object | Player builds workshop |
| `building_damaged` | 1-5 | ID list | Fire damages buildings |
| `flora_harvested` | 1-10 | ID list | Colonist chops trees |
| `area_fire` | 10-100 | ID list + area | Fire spreads across area |
| `plant_plague` | 100-10000 | ID range or area | Blight kills crops |
| `meteor_strike` | 50-500 | Area + ID list | Meteor destroys area |
| `construction_batch` | 5-20 | ID list | Multiple buildings queued |

#### World Event Message

```json
{
  "type": "world_event",
  "event": "colonist_died",
  "timestamp": 1729800002,
  "colonistId": 100,
  "cause": "starvation",
  "location": {"x": 45, "y": 67}
}
```

### Protocol Implementation (cpp-httplib + WebSocket)

**Server-side:**

```cpp
class GameServer {
    httplib::Server m_httpServer;
    std::vector<WebSocketClient> m_wsClients;
    std::thread m_networkThread;

public:
    void StartNetworking(int port = 9000) {
        // HTTP endpoints
        m_httpServer.Post("/api/game/create", HandleCreateGame);
        m_httpServer.Post("/api/game/join/:id", HandleJoinGame);
        m_httpServer.Get("/api/game/state", HandleGetState);
        m_httpServer.Get("/api/world/chunk/:x/:y", HandleGetChunk);
        m_httpServer.Get("/api/health", HandleHealth);
        m_httpServer.Post("/api/shutdown", HandleShutdown);

        // WebSocket upgrade endpoint
        m_httpServer.Get("/ws/:token", HandleWebSocketUpgrade);

        // Start network thread (localhost only for Phase 1)
        m_networkThread = std::thread([this, port]() {
            LOG_INFO("Network", "Game server listening on localhost:%d", port);
            m_httpServer.listen("127.0.0.1", port);
        });
    }

    void BroadcastStateUpdate() {
        // Called every tick (60 Hz) from game thread
        StateUpdate update = BuildStateUpdate();
        std::string json = update.ToJSON();

        // Send to all connected WebSocket clients
        for (auto& client : m_wsClients) {
            client.Send(json);
        }
    }

    void BroadcastBatchUpdate(BatchUpdate update) {
        // Called for mass events (plague, fire, meteor)
        std::string json = update.ToJSON();

        for (auto& client : m_wsClients) {
            client.Send(json);
        }
    }
};
```

**Client-side:**

```cpp
class GameClient {
    std::string m_playerToken;
    WebSocket m_ws;
    std::thread m_networkThread;

public:
    void Connect(const std::string& host, int port) {
        // Create game via HTTP
        auto response = httplib::Client(host, port).Post("/api/game/create",
            R"({"worldSeed": 12345, "colonyName": "New Hope"})",
            "application/json");

        json data = json::parse(response->body);
        m_playerToken = data["playerToken"];

        // Connect WebSocket
        std::string wsUrl = "ws://" + host + ":" + std::to_string(port) +
                            "/ws/" + m_playerToken;
        m_ws.Connect(wsUrl);

        // Start network thread for receiving state updates
        m_networkThread = std::thread([this]() {
            while (m_ws.IsConnected()) {
                std::string message = m_ws.Receive();
                HandleServerMessage(message);
            }
        });
    }

    void SendInput(const std::vector<PlayerAction>& actions) {
        // Called when player issues orders
        json msg = {
            {"type", "input"},
            {"timestamp", GetTimestamp()},
            {"sequence", m_inputSequence++},
            {"actions", SerializeActions(actions)}
        };
        m_ws.Send(msg.dump());
    }

    void HandleServerMessage(const std::string& jsonMsg) {
        json msg = json::parse(jsonMsg);
        std::string type = msg["type"];

        if (type == "state_update") {
            ApplyStateUpdate(msg);
        } else if (type == "batch_update") {
            ApplyBatchUpdate(msg);
        } else if (type == "world_event") {
            HandleWorldEvent(msg);
        }
    }

    void ApplyBatchUpdate(const json& msg) {
        std::string updateType = msg["updateType"];

        if (updateType == "plant_plague") {
            // Mark affected flora as dead
            auto& affected = msg["affectedObjects"];
            std::string targetType = affected["targetType"];

            if (targetType == "id_list") {
                for (int id : affected["ids"]) {
                    auto* flora = m_world.GetFlora(id);
                    if (flora) {
                        flora->health = 0;
                        flora->alive = false;
                    }
                }
            } else if (targetType == "area") {
                // Re-request affected chunks for fresh data
                for (auto& chunkInfo : affected["affectedChunks"]) {
                    RequestChunk(chunkInfo["x"], chunkInfo["y"]);
                }
            }
        }
    }
};
```

## Threading Model

### Server Threading

```
Main Thread (Game Logic):
  - Tick colony simulation at 60 TPS (16.6ms budget)
  - Update ECS systems (colonist AI, needs, pathfinding)
  - Process validated player inputs (construction orders, harvesting)
  - Simulate flora growth, structure construction progress
  - Generate world chunks on-demand
  - Build state updates for network
  - Send to network thread via queue

Network Thread:
  - HTTP server (listening for new connections, chunk requests)
  - WebSocket I/O (receive inputs, send updates)
  - Read input from clients → queue for main thread
  - Read state updates from queue → broadcast to clients
  - Handle batch updates (large events)

World Gen Thread:
  - Async chunk generation (terrain, flora placement, structures)
  - Triggered by chunk requests
  - Results sent to main thread via queue

Save Thread (optional):
  - Async world serialization
  - Triggered every 5 minutes (autosave)
  - Saves to disk without blocking game loop
```

**Inter-thread Communication:**

Lock-free queues for all cross-thread data:

```cpp
class GameServer {
    // Network → Game thread
    LockFreeQueue<PlayerInput> m_inputQueue;

    // Game → Network thread
    LockFreeQueue<StateUpdate> m_updateQueue;
    LockFreeQueue<BatchUpdate> m_batchUpdateQueue;

    // Main game loop (60 TPS)
    void Tick(float dt) {
        // 1. Process inputs from network
        PlayerInput input;
        while (m_inputQueue.TryPop(input)) {
            ValidateAndApplyInput(input);
        }

        // 2. Update colony simulation
        m_ecs.Update(dt);
        UpdateFloraGrowth(dt);
        UpdateConstructionProgress(dt);

        // 3. Generate state update
        StateUpdate update = BuildStateUpdate();
        m_updateQueue.Push(update);

        // 4. Check for batch events
        if (m_eventSystem.HasBatchEvents()) {
            for (auto& batchEvent : m_eventSystem.GetBatchEvents()) {
                m_batchUpdateQueue.Push(batchEvent);
            }
        }
    }

    // Network thread
    void NetworkLoop() {
        while (running) {
            // Receive inputs from clients
            for (auto& client : m_wsClients) {
                if (client.HasMessage()) {
                    PlayerInput input = ParseInput(client.Receive());
                    m_inputQueue.Push(input);
                }
            }

            // Send state updates to clients
            StateUpdate update;
            while (m_updateQueue.TryPop(update)) {
                std::string json = update.ToJSON();
                BroadcastToClients(json);
            }

            // Send batch updates
            BatchUpdate batch;
            while (m_batchUpdateQueue.TryPop(batch)) {
                std::string json = batch.ToJSON();
                BroadcastToClients(json);
            }
        }
    }
};
```

### Client Threading

```
Main Thread (Rendering):
  - Render at 60 FPS
  - Capture input (selection, orders)
  - Send input to network thread
  - Apply state updates from network thread
  - Update entity positions (interpolation)
  - Render SVG-based structures/flora
  - UI updates

Network Thread:
  - WebSocket I/O
  - Send player input
  - Receive state updates (60 Hz)
  - Receive batch updates
  - Pass updates to main thread via queue
```

## State Synchronization for Colony Sim

### Entity Interpolation (Colonists, Animals)

Entities move constantly and need smooth rendering:

```cpp
class ClientEntity {
    struct PositionSnapshot {
        Vector2 position;
        uint64_t timestamp;
    };

    std::deque<PositionSnapshot> m_snapshots;

    void OnServerUpdate(EntityState state) {
        // Store snapshot
        m_snapshots.push_back({state.position, state.timestamp});

        // Keep last 10 snapshots (166ms at 60 Hz)
        if (m_snapshots.size() > 10) {
            m_snapshots.pop_front();
        }
    }

    Vector2 GetInterpolatedPosition() const {
        if (m_snapshots.size() < 2) {
            return m_snapshots.back().position;
        }

        // Render 100ms in the past
        uint64_t renderTime = GetCurrentTime() - 100;

        // Find two snapshots to interpolate between
        for (size_t i = 0; i < m_snapshots.size() - 1; i++) {
            if (m_snapshots[i].timestamp <= renderTime &&
                m_snapshots[i + 1].timestamp >= renderTime) {

                // Linear interpolation
                float t = (renderTime - m_snapshots[i].timestamp) /
                          (float)(m_snapshots[i + 1].timestamp - m_snapshots[i].timestamp);

                return Lerp(m_snapshots[i].position,
                           m_snapshots[i + 1].position, t);
            }
        }

        return m_snapshots.back().position;
    }
};
```

**Result**: Smooth colonist/animal movement despite discrete network updates.

### Structure Synchronization (Buildings, Ruins)

Structures don't move, but have changing properties:

```cpp
class ClientStructure {
    uint32_t m_id;
    std::string m_type;  // "workshop", "storage_shed"
    Vector2 m_position;
    float m_rotation;
    float m_constructionProgress;
    int m_health;
    int m_variant;  // SVG procedural variation seed

    // Client has SVG asset locally
    SVGAsset* m_svgAsset;
    Texture m_cachedRaster;  // Cached rasterization

    void OnServerUpdate(StructureUpdate update) {
        // Only update changed properties
        if (update.has_constructionProgress) {
            m_constructionProgress = update.constructionProgress;
            // Re-render with scaffolding overlay if incomplete
        }
        if (update.has_health) {
            m_health = update.health;
            // Add damage overlay if damaged
        }
    }

    void Render(Renderer& renderer) {
        // Render from cached rasterization
        renderer.DrawTexture(m_cachedRaster, m_position, m_rotation);

        // Overlay effects
        if (m_constructionProgress < 1.0f) {
            RenderConstructionScaffolding(m_constructionProgress);
        }
        if (m_health < 100) {
            RenderDamageEffects(m_health);
        }
    }
};
```

**SVG Asset Management:**
- Client has all SVG building definitions locally (`assets/buildings/workshop.svg`)
- Server only sends: building ID, type, position, rotation, variant seed
- Client rasterizes SVG with procedural variations (same system as terrain tiles)
- Rasterized textures cached aggressively (buildings don't change visual appearance often)

### Flora Synchronization (Plants, Trees)

Flora grows over time but changes are discrete events:

```cpp
class ClientFlora {
    uint32_t m_id;
    std::string m_type;  // "oak_tree", "wheat"
    Vector2 m_position;
    float m_growth;  // 0.0 - 1.0
    int m_health;
    bool m_alive;

    SVGAsset* m_svgAsset;
    Texture m_cachedRaster;

    void OnServerUpdate(FloraUpdate update) {
        if (update.has_growth) {
            m_growth = update.growth;
            // Re-rasterize at new scale (growth affects size)
            RegenerateRaster();
        }
        if (update.has_health) {
            m_health = update.health;
            if (m_health == 0) {
                m_alive = false;
                // Show dead/withered version
            }
        }
    }

    void OnBatchUpdate(BatchUpdate batch) {
        // Mass event (plague, fire)
        if (batch.affectedObjects.ContainsID(m_id)) {
            ApplyChanges(batch.affectedObjects.changes);
        }
    }

    void RegenerateRaster() {
        // Rasterize SVG at scale based on growth
        float scale = 0.2f + (m_growth * 0.8f);  // 20% -> 100% size
        m_cachedRaster = m_svgAsset->Rasterize(scale, m_variant);
    }
};
```

**Flora Update Patterns:**
- **Growth**: Server sends discrete growth updates (seedling → sapling → mature), not every tick
- **Harvest**: Immediate removal event
- **Plague/Fire**: Batch update affecting hundreds/thousands at once

## Chunk Streaming

### On-Demand Loading

Client requests chunks as camera moves:

```cpp
class ClientWorld {
    std::unordered_map<ChunkCoord, Chunk> m_loadedChunks;
    std::set<ChunkCoord> m_pendingRequests;

    void Update(const Camera& camera) {
        // Calculate visible chunk range (viewport + 2 chunk buffer)
        ChunkRect visibleChunks = CalculateVisibleChunks(camera);

        for (int y = visibleChunks.minY; y <= visibleChunks.maxY; y++) {
            for (int x = visibleChunks.minX; x <= visibleChunks.maxX; x++) {
                ChunkCoord coord = {x, y};

                // Already loaded or requested?
                if (m_loadedChunks.count(coord) || m_pendingRequests.count(coord)) {
                    continue;
                }

                // Request from server
                RequestChunk(coord);
                m_pendingRequests.insert(coord);
            }
        }

        // Unload distant chunks (LRU eviction, max 1000 chunks)
        EvictDistantChunks(camera, 1000);
    }

    void RequestChunk(ChunkCoord coord) {
        // HTTP request (async)
        m_http.GetAsync("/api/world/chunk/" + std::to_string(coord.x) + "/" +
                        std::to_string(coord.y),
                        [this, coord](const std::string& response) {
            Chunk chunk = ParseChunkData(response);
            m_loadedChunks[coord] = chunk;
            m_pendingRequests.erase(coord);

            // Rasterize terrain tiles
            RasterizeTerrainTiles(chunk);

            // Instantiate flora/structures from metadata
            InstantiateFlora(chunk.flora);
            InstantiateStructures(chunk.structures);
        });
    }
};
```

### Server-Side Chunk Generation

```cpp
void GameServer::HandleGetChunk(const httplib::Request& req, httplib::Response& res) {
    int x = std::stoi(req.matches[1]);
    int y = std::stoi(req.matches[2]);

    // Check cache first
    if (m_chunkCache.Has({x, y})) {
        Chunk& chunk = m_chunkCache.Get({x, y});
        res.set_content(chunk.ToJSON(), "application/json");
        return;
    }

    // Generate async (may take 100-500ms)
    // - Generate terrain (Perlin noise)
    // - Place flora (procedural, biome-dependent)
    // - Place structures (ruins, natural formations)
    auto future = m_worldGen.GenerateChunkAsync(x, y);
    Chunk chunk = future.get();

    // Cache and return
    m_chunkCache.Set({x, y}, chunk);
    res.set_content(chunk.ToJSON(), "application/json");
}
```

## Performance Requirements

### Server Tick Budget (60 TPS = 16.6ms)

```
ECS Update:             6ms  (colonist AI, needs, pathfinding)
Colony Simulation:      3ms  (flora growth, construction, production chains)
Input Validation:       1ms  (validate player actions)
World Gen Requests:     0.5ms (queue chunk generation)
State Update Build:     2ms  (serialize visible entities/structures)
Network Send:           1ms  (broadcast to clients)
Misc/Buffer:            3ms
────────────────────────────
Total:                  16.6ms
```

### Client Frame Budget (60 FPS = 16.6ms)

```
Input Capture:          0.5ms
Network Receive:        1ms  (process state updates)
Entity Interpolation:   1ms  (smooth colonist movement)
SVG Rasterization:      2ms  (new structures, cache misses)
Rendering:              9ms  (OpenGL draw calls, terrain + structures + entities)
UI:                     2ms  (colony UI, menus)
Misc/Buffer:            1ms
────────────────────────────
Total:                  16.6ms
```

### Bandwidth Budget

**Per client:**
- Input: 10 Hz × 500 bytes = 5 KB/s upload (orders sent when issued, not every frame)
- State updates: 60 Hz × 2 KB = 120 KB/s download (entities + structure deltas)
- Chunk requests: On-demand, ~50 KB per chunk (amortized: 5 KB/s)
- **Total: ~130 KB/s download, ~5 KB/s upload**

**Server with 10 clients:**
- Receive inputs: 10 × 5 KB/s = 50 KB/s
- Broadcast states: 10 × 120 KB/s = 1.2 MB/s
- **Total: ~1.25 MB/s (10 Mbps)**

Well within localhost bandwidth (100+ Mbps).

## WebSocket Library Choice

**Recommendation: cpp-httplib (same as debug server)**

**Rationale:**
- Already using for debug server and HTTP endpoints
- Supports WebSocket upgrades
- Header-only, simple integration
- Good enough for Phase 1 & 2 (localhost, <50 players)

**Alternative for Phase 3 (if needed):**
- **uWebSockets**: Extremely fast, scales to thousands of players
- More complex, but necessary for dedicated servers

**Integration:**
```cmake
# Already in vcpkg.json for debug server
{
  "dependencies": ["cpp-httplib"]
}

# libs/engine/server/CMakeLists.txt
find_package(httplib CONFIG REQUIRED)
target_link_libraries(engine-server PRIVATE httplib::httplib)
```

## Future Enhancements

### Phase 2: Local Multiplayer
- Server runs as daemon (detached from client)
- Multiple clients connect to same local server
- Shared colony (co-op) or separate colonies (competitive)
- LAN discovery (broadcast server presence on network)
- Player permissions (who can issue orders to which colonists)

### Phase 3: Online Multiplayer
- Dedicated servers (cloud deployment)
- Matchmaking system, server browser
- Authentication (accounts, login, session tokens)
- Anti-cheat (server-side validation of all actions)
- Spectator mode (read-only clients watching games)
- Replay system (record game state for playback)

### Protocol Optimizations
- **Binary protocol**: Replace JSON with MessagePack or Protobuf
- **Delta compression**: Only send changed entity fields (not full state)
- **Interest management**: Only send entities/structures in player's view range
- **Snapshot compression**: Use reference snapshots + deltas

### Gameplay Features Enabled by Multiplayer
- **Trading between colonies**: Resource exchange, caravans
- **Shared world events**: Plagues, meteor showers affecting all players
- **PvP raids**: Players attack each other's colonies
- **Co-op colony management**: Multiple players control same colony
- **Asynchronous play**: Leave server running, players join/leave

## Related Documentation

- [Game Design Doc: Multiplayer Architecture](/docs/design/features/multiplayer/README.md)
- [Process Management](./process-management.md) - Client spawns/monitors server
- [HTTP Debug Server](./http-debug-server.md) - Separate debugging protocol (port 8080)
- [ECS Design](./cpp-coding-standards.md#ecs) - Entity component system
- [World Generation Architecture](./world-generation-architecture.md) - Chunk generation
- [Vector Asset Pipeline](./vector-asset-pipeline.md) - SVG rendering system
