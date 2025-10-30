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

#include "debug/lock_free_ring_buffer.h"
#include "metrics/performance_metrics.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// Forward declare httplib::Server to avoid exposing cpp-httplib in header
namespace httplib {
	class Server;
}

namespace Foundation {

	// Control actions for sandbox control endpoint
	enum class ControlAction { None, Exit, SceneChange, Pause, Resume, ReloadScene };

	// Log levels (must match foundation::LogLevel enum)
	enum class LogLevel { Debug, Info, Warning, Error };

	// Log categories (must match foundation::LogCategory enum)
	enum class LogCategory { Renderer, Physics, Audio, Network, Game, World, UI, Engine, Foundation };

	// Log entry for HTTP streaming
	struct LogEntry {
		LogLevel	level;
		LogCategory category;
		char		message[256];
		uint64_t	timestamp; // Unix timestamp in milliseconds
		const char* file;	   // Pointer to static string (filename)
		int			line;

		// Serialize to JSON
		const char* ToJSON() const;
	};

	class DebugServer {
	  public:
		DebugServer();
		~DebugServer();

		// Start HTTP server on specified port (runs in separate thread)
		void Start(int port);

		// Stop HTTP server and join thread
		void Stop();

		// Update metrics (called from game thread)
		void UpdateMetrics(const PerformanceMetrics& metrics);

		// Update logs (called from game thread) - NEVER BLOCKS
		// If buffer is full, oldest logs are dropped silently
		void UpdateLog(LogLevel level, LogCategory category, const char* message, const char* file, int line);

		// Check if server is running
		bool IsRunning() const { return m_running.load(); }

		// Screenshot capture (called from main thread with GL context)
		// Checks if screenshot is requested, captures framebuffer if so
		void CaptureScreenshotIfRequested();

		// Request screenshot (called from HTTP thread)
		// Returns true if screenshot was captured, false if timeout
		bool RequestScreenshot(std::vector<unsigned char>& pngData, int timeoutMs = 5000);

		// Control action API (thread-safe, checked by main loop)
		ControlAction GetControlAction() const { return m_controlAction.load(); }
		void		  ClearControlAction() { m_controlAction.store(ControlAction::None); }

		// Get target scene name (for SceneChange action)
		std::string GetTargetSceneName() const;

	  private:
		std::unique_ptr<httplib::Server> m_server;
		std::thread						 m_serverThread;
		std::atomic<bool>				 m_running;

		// Lock-free metrics buffer (game thread writes, HTTP thread reads)
		LockFreeRingBuffer<PerformanceMetrics, 64> m_metricsBuffer;

		// Lock-free log buffer (game thread writes, HTTP thread reads)
		// Size: 1000 entries. If full, oldest logs dropped (circular buffer)
		LockFreeRingBuffer<LogEntry, 1000> m_logBuffer;

		// Screenshot request/response synchronization
		std::atomic<bool>		   m_screenshotRequested{false};
		std::atomic<bool>		   m_screenshotReady{false};
		std::vector<unsigned char> m_screenshotData;
		std::mutex				   m_screenshotMutex; // Protects m_screenshotData

		// Control action state (HTTP thread writes, main thread reads)
		std::atomic<ControlAction> m_controlAction{ControlAction::None};
		std::string				   m_targetSceneName;
		mutable std::mutex		   m_sceneNameMutex; // Protects m_targetSceneName

		// Server thread entry point
		void ServerThreadFunc(int port);

		// Helper: Get metrics snapshot (lock-free read)
		PerformanceMetrics GetMetricsSnapshot() const;
	};

} // namespace Foundation
