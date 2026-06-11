#pragma once

// HTTP Debug Server - Serves performance metrics and logs via HTTP/SSE.
//
// This server runs on a separate thread and provides:
// - REST endpoints for current metrics snapshots
// - Server-Sent Events for real-time metric streaming
// - Real-time log streaming
//
// Lock-free design: Game thread writes to ring buffer (never blocks),
// HTTP thread reads latest sample. Zero mutex contention.
//
// CRITICAL: If ring buffer is full, oldest entries are dropped.
// Performance > Complete Logs. Never blocks game thread.

#include "debug/LockFreeRingBuffer.h"
#include "metrics/PerformanceMetrics.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// Forward declare httplib::Server to avoid exposing cpp-httplib in header
namespace httplib {
	class Server;
}

namespace Foundation { // NOLINT(readability-identifier-naming)

	// Control actions for sandbox control endpoint
	enum class ControlAction : std::uint8_t { None, Exit, SceneChange, Pause, Resume, ReloadScene, SetVsync }; // NOLINT(performance-enum-size)

	// Remote camera command (set via /api/control?action=camera).
	// Fields are optional: only the parameters present in the request are applied.
	// Pan is a persistent direction (-1..1 per axis) applied every frame like held
	// movement keys; send panx=0&pany=0 to stop.
	struct CameraCommand {
		bool  hasPosition = false;
		float x = 0.0F;
		float y = 0.0F;
		bool  hasZoom = false;
		float zoom = 0.0F;
		bool  hasPan = false;
		float panX = 0.0F;
		float panY = 0.0F;
	};

	// Synthetic input injected via /api/input. Coordinates are logical UI
	// pixels (same space the screenshot endpoint captures). Click expands to
	// move+down+up at dispatch so single-shot interactions are one request;
	// multi-frame gestures (drags) use separate down/move/up requests.
	struct InputCommand {
		enum class Type : std::uint8_t { Move, Down, Up, Click, Scroll };
		Type		 type{Type::Click};
		float		 x = 0.0F;
		float		 y = 0.0F;
		std::uint8_t button = 0; // 0=left, 1=right, 2=middle
		float		 scrollDelta = 0.0F;
	};

	// Log levels (must match foundation::LogLevel enum)
	enum class LogLevel : std::uint8_t { Debug, Info, Warning, Error }; // NOLINT(performance-enum-size)

	// Log categories (must match foundation::LogCategory enum)
	enum class LogCategory : std::uint8_t { // NOLINT(performance-enum-size)
		Renderer,
		Physics,
		Audio,
		Network,
		Game,
		World,
		UI,
		Engine,
		Foundation
	}; // NOLINT(performance-enum-size)

	// Log entry for HTTP streaming
	struct LogEntry { // NOLINT(cppcoreguidelines-pro-type-member-init)
		LogLevel	level{};
		LogCategory category{};
		char		message[256]{};
		uint64_t	timestamp{}; // Unix timestamp in milliseconds
		const char* file{};		 // Pointer to static string (filename)
		int			line{};

		// Serialize to JSON
		const char* toJSON() const;
	};

	class DebugServer {
	  public:
		DebugServer();
		~DebugServer();

		// Delete copy operations (not copyable due to unique_ptr and thread)
		DebugServer(const DebugServer&) = delete;
		DebugServer& operator=(const DebugServer&) = delete;

		// Delete move operations (not movable due to thread)
		DebugServer(DebugServer&&) = delete;
		DebugServer& operator=(DebugServer&&) = delete;

		// Start HTTP server on specified port (runs in separate thread)
		void start(int port);

		// Stop HTTP server and join thread
		void stop();

		// Update metrics (called from game thread)
		void updateMetrics(const PerformanceMetrics& metrics);

		// Update logs (called from game thread) - NEVER BLOCKS
		// If buffer is full, oldest logs are dropped silently
		void updateLog(LogLevel level, LogCategory category, const char* message, const char* file, int line);

		// Check if server is running
		bool isRunning() const { return running.load(); }

		// Screenshot capture (called from main thread with GL context)
		// Checks if screenshot is requested, captures framebuffer if so
		void captureScreenshotIfRequested();

		// Request screenshot (called from HTTP thread)
		// Returns true if screenshot was captured, false if timeout
		bool requestScreenshot(std::vector<unsigned char>& pngData, int timeoutMs = 5000);

		// Control action API (thread-safe, checked by main loop)
		ControlAction getControlAction() const { return controlAction.load(); }
		void		  clearControlAction() { controlAction.store(ControlAction::None); }

		// Get target scene name (for SceneChange action)
		std::string getTargetSceneName() const;

		// Get target vsync interval (for SetVsync action): 0 = off, 1 = on
		int getTargetVsync() const { return targetVsync.load(); }

		// Consume pending camera command (game thread). Returns true if a new
		// command was written since the last consume.
		bool consumeCameraCommand(CameraCommand& out);

		// Consume queued synthetic input (game thread). Appends to out and
		// returns true if any commands were pending.
		bool consumeInputCommands(std::vector<InputCommand>& out);

		// Set/get current scene name (for metrics streaming to frontend)
		void		setCurrentSceneName(const std::string& name);
		std::string getCurrentSceneName() const;

		// Shutdown synchronization (for graceful exit via HTTP)
		// Called by main loop after all cleanup is complete
		void signalShutdownComplete();

	  private:
		// Called by exit handler to block until cleanup is done
		void waitForShutdownComplete();
		std::unique_ptr<httplib::Server> server;
		std::thread						 serverThread;
		std::atomic<bool>				 running;

		// Lock-free metrics buffer (game thread writes, HTTP thread reads)
		LockFreeRingBuffer<PerformanceMetrics, 64> metricsBuffer;

		// Lock-free log buffer (game thread writes, HTTP thread reads)
		// Size: 1000 entries. If full, oldest logs dropped (circular buffer)
		LockFreeRingBuffer<LogEntry, 1000> logBuffer;

		// Screenshot request/response synchronization
		std::atomic<bool>		   screenshotRequested{false};
		std::atomic<bool>		   screenshotReady{false};
		std::vector<unsigned char> screenshotData;
		std::mutex				   screenshotMutex; // Protects screenshotData

		// Control action state (HTTP thread writes, main thread reads)
		std::atomic<ControlAction> controlAction{ControlAction::None};
		std::string				   targetSceneName;
		std::string				   currentSceneName;
		mutable std::mutex		   sceneNameMutex; // Protects targetSceneName and currentSceneName

		// Camera command state (HTTP thread writes, game thread consumes)
		CameraCommand	   cameraCommand;
		std::atomic<bool>  cameraCommandPending{false};
		mutable std::mutex cameraCommandMutex; // Protects cameraCommand

		// Synthetic input queue (HTTP thread writes, game thread consumes)
		std::vector<InputCommand> inputCommands;
		std::atomic<bool>		  inputCommandsPending{false};
		mutable std::mutex		  inputCommandsMutex; // Protects inputCommands

		// Vsync target for SetVsync action (0 = off, 1 = on)
		std::atomic<int> targetVsync{1};

		// Shutdown synchronization (for blocking exit handler until cleanup done)
		mutable std::mutex		shutdownMutex;
		std::condition_variable shutdownCV;
		bool					shutdownComplete{false};
		bool					handlerDone{false}; // True when exit handler has finished

		// Server thread entry point
		void serverThreadFunc(int port);

		// Helper: Get metrics snapshot (lock-free read)
		PerformanceMetrics getMetricsSnapshot() const;
	};

} // namespace Foundation
