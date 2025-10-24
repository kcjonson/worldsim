# Process Management - Technical Design

## Overview

The game client (`world-sim`) spawns the game server (`world-sim-server`) only when needed - when starting a new game or loading a saved game. The server shuts down when returning to the main menu. This minimizes resource usage and keeps the architecture clean.

**Goal**: Transparent client/server architecture where the server only runs during active gameplay.

## Workflow Overview

```
User launches world-sim
       ↓
[Main Menu]
  - New Game
  - Load Game
  - Settings
  - Quit
       ↓
Player clicks "New Game" or "Load Game"
       ↓
[Spawn Server]
       ↓
Check: Is server running on port 9000?
       ↓
    YES → Error: "Port 9000 in use, close other instance"
       ↓
    NO → Spawn world-sim-server subprocess
       ↓
Wait for GET /api/health (retry 10x, 500ms interval)
       ↓
   FAIL → Error: "Server failed to start"
       ↓
     OK → Create/Load game via HTTP
       ↓
Connect WebSocket ws://localhost:9000/ws/{token}
       ↓
[Game Running]
       ↓
Background: HTTP health check every 5s
       ↓
  FAIL → "Server crashed" dialog
       ↓
[Player Quits to Menu]
       ↓
POST /api/shutdown
       ↓
Wait 5s for server exit
       ↓
If still running: Force kill
       ↓
Reap zombie (Unix)
       ↓
[Back to Main Menu]
       ↓
(Server is not running)
       ↓
[Player Quits Application]
       ↓
(Nothing to clean up, just exit)
```

## Lifecycle States

### State 1: Main Menu (No Server)

**Client state:**
- Main menu UI active
- No server process running
- No network connections

**Player actions:**
- Browse menus, change settings
- Start new game → Transition to State 2
- Load saved game → Transition to State 2
- Quit → Exit application

### State 2: Starting Game (Server Spawning)

**Client actions:**
1. Spawn `world-sim-server` process
2. Wait for HTTP `/api/health` to respond (up to 5 seconds)
3. Send `POST /api/game/create` or `POST /api/game/load`
4. Receive player token
5. Connect WebSocket
6. Transition to State 3

**Failure modes:**
- Server executable not found → Error dialog, return to menu
- Server fails to start → Error dialog with logs, return to menu
- Game creation fails → Error dialog, shutdown server, return to menu

### State 3: Playing Game (Server Running)

**Client state:**
- Server process running (child)
- WebSocket connected
- Health checks every 5 seconds (background)
- Gameplay active

**Player actions:**
- Play game normally
- Quit to menu → Transition to State 4

**Failure modes:**
- Server crashes → Dialog with restart/quit options
- Connection lost → Dialog with reconnect/quit options

### State 4: Returning to Menu (Server Shutdown)

**Client actions:**
1. Close WebSocket
2. Send `POST /api/shutdown` to server
3. Wait up to 5 seconds for server process to exit
4. Force kill if timeout
5. Reap zombie process (Unix)
6. Return to State 1 (main menu)

### State 5: Quitting Application

**From main menu (State 1):**
- Just exit (no server to clean up)

**From playing game (State 3):**
- Same as State 4 (shutdown server first)
- Then exit

## Cross-Platform Process Spawning

### Platform-Specific APIs

```cpp
class ProcessManager {
#ifdef _WIN32
    PROCESS_INFORMATION m_processInfo;
    HANDLE m_processHandle;
#else
    pid_t m_serverPID;
#endif

    bool m_serverRunning = false;

public:
    bool SpawnServer(const std::string& executablePath, int port);
    bool IsProcessRunning() const;
    void WaitForExit(int timeoutMs);
    void ForceKill();
    ~ProcessManager();
};
```

### Windows Implementation

```cpp
bool ProcessManager::SpawnServer(const std::string& executablePath, int port) {
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;  // Hidden window (headless)

    PROCESS_INFORMATION pi = {};

    // Build command line
    std::string cmdLine = executablePath + " --port " + std::to_string(port);

    // Spawn process
    if (!CreateProcessA(
        nullptr,
        const_cast<char*>(cmdLine.c_str()),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NEW_CONSOLE,
        nullptr,
        nullptr,
        &si,
        &pi
    )) {
        LOG_ERROR("Process", "Failed to spawn server: %d", GetLastError());
        return false;
    }

    m_processInfo = pi;
    m_processHandle = pi.hProcess;
    m_serverRunning = true;

    LOG_INFO("Process", "Server spawned with PID %d", pi.dwProcessId);
    return true;
}

bool ProcessManager::IsProcessRunning() const {
    if (!m_serverRunning || !m_processHandle) return false;

    DWORD exitCode;
    if (GetExitCodeProcess(m_processHandle, &exitCode)) {
        return exitCode == STILL_ACTIVE;
    }
    return false;
}

void ProcessManager::WaitForExit(int timeoutMs) {
    if (!m_processHandle) return;
    WaitForSingleObject(m_processHandle, timeoutMs);
}

void ProcessManager::ForceKill() {
    if (!m_processHandle) return;

    LOG_WARNING("Process", "Force-killing server process");
    TerminateProcess(m_processHandle, 1);
    WaitForSingleObject(m_processHandle, 5000);
    m_serverRunning = false;
}

ProcessManager::~ProcessManager() {
    if (m_processHandle) {
        CloseHandle(m_processHandle);
        CloseHandle(m_processInfo.hThread);
    }
}
```

### Unix (macOS/Linux) Implementation

```cpp
bool ProcessManager::SpawnServer(const std::string& executablePath, int port) {
    pid_t pid = fork();

    if (pid == -1) {
        LOG_ERROR("Process", "Failed to fork: %s", strerror(errno));
        return false;
    }

    if (pid == 0) {
        // Child process
        std::string portArg = "--port";
        std::string portValue = std::to_string(port);

        execl(executablePath.c_str(),
              "world-sim-server",
              portArg.c_str(),
              portValue.c_str(),
              nullptr);

        // If execl returns, it failed
        LOG_ERROR("Process", "Failed to exec server: %s", strerror(errno));
        exit(1);
    }

    // Parent process
    m_serverPID = pid;
    m_serverRunning = true;
    LOG_INFO("Process", "Server spawned with PID %d", pid);
    return true;
}

bool ProcessManager::IsProcessRunning() const {
    if (!m_serverRunning || m_serverPID <= 0) return false;

    // Send signal 0 to check if process exists
    int result = kill(m_serverPID, 0);
    return (result == 0);
}

void ProcessManager::WaitForExit(int timeoutMs) {
    if (m_serverPID <= 0) return;

    int status;
    int elapsed = 0;

    // Poll every 100ms up to timeout
    while (elapsed < timeoutMs) {
        pid_t result = waitpid(m_serverPID, &status, WNOHANG);
        if (result == m_serverPID) {
            LOG_INFO("Process", "Server exited with status %d", WEXITSTATUS(status));
            m_serverPID = 0;
            m_serverRunning = false;
            return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        elapsed += 100;
    }
}

void ProcessManager::ForceKill() {
    if (m_serverPID <= 0) return;

    LOG_WARNING("Process", "Force-killing server process %d", m_serverPID);
    kill(m_serverPID, SIGKILL);

    // Reap zombie
    int status;
    waitpid(m_serverPID, &status, 0);
    m_serverPID = 0;
    m_serverRunning = false;
}

ProcessManager::~ProcessManager() {
    if (m_serverPID > 0) {
        int status;
        waitpid(m_serverPID, &status, WNOHANG);
    }
}
```

### Preventing Zombie Processes (Unix)

**Solution - Signal Handler:**

```cpp
void SetupChildReaper() {
    struct sigaction sa;
    sa.sa_handler = [](int) {
        while (waitpid(-1, nullptr, WNOHANG) > 0);
    };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if (sigaction(SIGCHLD, &sa, nullptr) == -1) {
        LOG_ERROR("Process", "Failed to install SIGCHLD handler");
    }
}

void ClientApp::Initialize() {
    #ifndef _WIN32
    SetupChildReaper();
    #endif
}
```

## Starting a Game

### New Game Flow

```cpp
void ClientApp::OnNewGameClicked() {
    // Show loading screen
    ShowLoadingScreen("Starting server...");

    // Start server
    if (!StartServer()) {
        HideLoadingScreen();
        ShowError("Failed to start server");
        return;
    }

    // Create game
    ShowLoadingScreen("Creating world...");
    if (!CreateGame()) {
        HideLoadingScreen();
        ShutdownServer();
        ShowError("Failed to create game");
        return;
    }

    // Connect and start playing
    ShowLoadingScreen("Connecting...");
    if (!ConnectToGame()) {
        HideLoadingScreen();
        ShutdownServer();
        ShowError("Failed to connect to game");
        return;
    }

    HideLoadingScreen();
    StartGameplay();
}

bool ClientApp::StartServer() {
    // Find server executable
    std::string serverPath = FindServerExecutable();
    if (serverPath.empty()) {
        return false;
    }

    // Check port not already in use
    if (CheckPortInUse(9000)) {
        ShowError("Port 9000 is already in use.\n"
                 "Close other instances of the game.");
        return false;
    }

    // Spawn server
    if (!m_processManager.SpawnServer(serverPath, 9000)) {
        return false;
    }

    // Wait for server to be ready
    for (int i = 0; i < 10; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if (CheckServerReady()) {
            LOG_INFO("Client", "Server ready after %d attempts", i + 1);
            m_healthMonitor.Start("localhost", 9000, [this]() {
                OnServerCrashed();
            });
            return true;
        }
    }

    // Timeout
    m_processManager.ForceKill();
    ShowError("Server failed to start.\nCheck logs at: " + GetServerLogPath());
    return false;
}

bool ClientApp::CreateGame() {
    httplib::Client client("localhost", 9000);
    client.set_connection_timeout(5, 0);

    json request = {
        {"worldSeed", GenerateRandomSeed()},
        {"colonyName", m_newGameSettings.colonyName}
    };

    auto response = client.Post("/api/game/create",
                               request.dump(),
                               "application/json");

    if (!response || response->status != 200) {
        LOG_ERROR("Client", "Failed to create game");
        return false;
    }

    json data = json::parse(response->body);
    m_playerToken = data["playerToken"];
    m_gameId = data["gameId"];

    return true;
}

bool ClientApp::ConnectToGame() {
    std::string wsUrl = "ws://localhost:9000/ws/" + m_playerToken;

    if (!m_websocket.Connect(wsUrl)) {
        LOG_ERROR("Client", "Failed to connect WebSocket");
        return false;
    }

    // Start network thread
    m_networkThread = std::thread([this]() {
        while (m_websocket.IsConnected()) {
            std::string message = m_websocket.Receive();
            HandleServerMessage(message);
        }
    });

    return true;
}
```

### Load Game Flow

```cpp
void ClientApp::OnLoadGameClicked(const std::string& savePath) {
    ShowLoadingScreen("Starting server...");

    if (!StartServer()) {
        HideLoadingScreen();
        ShowError("Failed to start server");
        return;
    }

    ShowLoadingScreen("Loading save...");
    if (!LoadGame(savePath)) {
        HideLoadingScreen();
        ShutdownServer();
        ShowError("Failed to load game");
        return;
    }

    ShowLoadingScreen("Connecting...");
    if (!ConnectToGame()) {
        HideLoadingScreen();
        ShutdownServer();
        ShowError("Failed to connect");
        return;
    }

    HideLoadingScreen();
    StartGameplay();
}

bool ClientApp::LoadGame(const std::string& savePath) {
    httplib::Client client("localhost", 9000);

    json request = {
        {"savePath", savePath}
    };

    auto response = client.Post("/api/game/load",
                               request.dump(),
                               "application/json");

    if (!response || response->status != 200) {
        return false;
    }

    json data = json::parse(response->body);
    m_playerToken = data["playerToken"];
    m_gameId = data["gameId"];

    return true;
}
```

## Returning to Menu

```cpp
void ClientApp::OnQuitToMenuClicked() {
    ShowLoadingScreen("Saving game...");

    // Close WebSocket
    if (m_websocket.IsConnected()) {
        m_websocket.Close();
    }

    // Wait for network thread
    if (m_networkThread.joinable()) {
        m_networkThread.join();
    }

    // Shutdown server
    ShutdownServer();

    HideLoadingScreen();
    ShowMainMenu();
}

void ClientApp::ShutdownServer() {
    // Stop health monitor
    m_healthMonitor.Stop();

    if (!m_processManager.IsProcessRunning()) {
        return;
    }

    LOG_INFO("Client", "Shutting down server...");

    // Request graceful shutdown
    httplib::Client client("localhost", 9000);
    client.set_connection_timeout(1, 0);
    auto response = client.Post("/api/shutdown", "", "application/json");

    if (response && response->status == 200) {
        LOG_INFO("Client", "Server acknowledged shutdown");
    }

    // Wait for exit
    m_processManager.WaitForExit(5000);

    if (m_processManager.IsProcessRunning()) {
        LOG_WARNING("Client", "Server did not exit, force killing");
        m_processManager.ForceKill();
    } else {
        LOG_INFO("Client", "Server exited cleanly");
    }
}
```

## Health Monitoring (During Gameplay Only)

```cpp
class ServerHealthMonitor {
    httplib::Client m_httpClient;
    std::thread m_monitorThread;
    std::atomic<bool> m_running{false};
    std::function<void()> m_onServerDied;

public:
    void Start(const std::string& host, int port, std::function<void()> callback) {
        m_httpClient = httplib::Client(host, port);
        m_onServerDied = callback;
        m_running = true;

        m_monitorThread = std::thread([this]() {
            while (m_running) {
                std::this_thread::sleep_for(std::chrono::seconds(5));

                if (!CheckHealth()) {
                    m_onServerDied();
                    break;
                }
            }
        });
    }

    bool CheckHealth() {
        auto result = m_httpClient.Get("/api/health");

        if (!result || result->status != 200) {
            return false;
        }

        try {
            json health = json::parse(result->body);
            return health["status"] == "ok";
        } catch (...) {
            return false;
        }
    }

    void Stop() {
        m_running = false;
        if (m_monitorThread.joinable()) {
            m_monitorThread.join();
        }
    }
};
```

## Error Handling

### Server Crash During Gameplay

```cpp
void ClientApp::OnServerCrashed() {
    // Pause game
    PauseGame();

    // Determine cause
    bool processAlive = m_processManager.IsProcessRunning();

    UIDialog dialog;
    if (processAlive) {
        dialog.SetTitle("Server Not Responding");
        dialog.SetMessage("The game server is not responding.\n"
                         "The process is still running but may be hung.");
    } else {
        dialog.SetTitle("Server Crashed");
        dialog.SetMessage("The game server has stopped unexpectedly.\n"
                         "Your progress may be lost.");
    }

    dialog.AddButton("Restart & Continue", [this]() {
        AttemptRestart();
    });

    dialog.AddButton("View Logs", []() {
        OpenFileInDefaultEditor(GetServerLogPath());
    });

    dialog.AddButton("Quit to Menu", [this]() {
        ShutdownServer();  // Force kill if necessary
        ShowMainMenu();
    });

    dialog.Show();
}

void ClientApp::AttemptRestart() {
    // Kill existing server
    if (m_processManager.IsProcessRunning()) {
        m_processManager.ForceKill();
    }

    // Restart server
    if (!StartServer()) {
        ShowError("Failed to restart server");
        ShowMainMenu();
        return;
    }

    // Try to reconnect (server will reload from autosave)
    if (ReconnectToGame()) {
        ResumeGame();
    } else {
        ShowError("Failed to reconnect");
        ShutdownServer();
        ShowMainMenu();
    }
}
```

### Port Already In Use

```cpp
bool ClientApp::CheckPortInUse(int port) {
    // Try to bind to port
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    int result = bind(sock, (sockaddr*)&addr, sizeof(addr));
    close(sock);

    return (result < 0);  // Port in use if bind failed
}
```

## Deployment Structure

### macOS (.app Bundle)

```
WorldSim.app/
  Contents/
    MacOS/
      world-sim          ← Client (starts in menu)
      world-sim-server   ← Server (spawned when needed)
    Resources/
      assets/
```

### Windows

```
WorldSim/
  world-sim.exe          ← Client
  world-sim-server.exe   ← Server (spawned by client)
  assets/
```

### Linux

```
/opt/world-sim/
  bin/
    world-sim
    world-sim-server
  share/world-sim/assets/
```

## Logging

### Server Logging (To File)

```cpp
void GameServer::InitializeLogging() {
    std::string logDir = GetUserDataDirectory() + "/.world-sim/logs/";
    CreateDirectoryIfNotExists(logDir);

    std::string logFile = logDir + "server.log";
    Logger::SetOutputFile(logFile);
    LOG_INFO("Server", "Logging to: %s", logFile.c_str());
}
```

**Log locations:**
- **Windows**: `%LOCALAPPDATA%\world-sim\logs\server.log`
- **macOS**: `~/Library/Application Support/world-sim/logs/server.log`
- **Linux**: `~/.local/share/world-sim/logs/server.log`

## Performance

### Startup Time (New Game)

```
Main menu displayed:         0ms (instant)
Player clicks "New Game":    0ms
Spawn server process:        50ms
Server initializes:          200ms
HTTP /api/health succeeds:   300ms
Create game (world gen):     1000ms (varies)
Connect WebSocket:           50ms
──────────────────────────────────
Total: ~1.6 seconds
```

**User sees**: "Starting server..." → "Creating world..." → Game starts

### Shutdown Time (Quit to Menu)

```
Player clicks "Quit to Menu": 0ms
Close WebSocket:              10ms
POST /api/shutdown:           50ms
Server saves game:            500ms
Server exits:                 50ms
Reap process:                 10ms
──────────────────────────────────
Total: ~620ms
```

**User sees**: "Saving game..." → Main menu

## Related Documentation

- [Multiplayer Architecture](./multiplayer-architecture.md) - Client/server protocol
- [Product Spec: Multiplayer](../specs/features/multiplayer/README.md) - Architecture overview
- [HTTP Debug Server](./http-debug-server.md) - Debug server (separate, port 8080)
