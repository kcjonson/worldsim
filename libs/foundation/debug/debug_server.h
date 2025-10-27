#pragma once

// HTTP Debug Server - Serves performance metrics via HTTP/SSE.
//
// This server runs on a separate thread and provides:
// - REST endpoints for current metrics snapshots
// - Server-Sent Events for real-time metric streaming
//
// Thread-safe: Game thread writes metrics, HTTP thread reads them.

#include "metrics/performance_metrics.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

// Forward declare httplib::Server to avoid exposing cpp-httplib in header
namespace httplib {
class Server;
}

namespace Foundation {

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

	// Check if server is running
	bool IsRunning() const { return m_running.load(); }

private:
	std::unique_ptr<httplib::Server> m_server;
	std::thread m_serverThread;
	std::atomic<bool> m_running;

	// Latest metrics (protected by mutex)
	PerformanceMetrics m_latestMetrics;
	mutable std::mutex m_metricsMutex;

	// Server thread entry point
	void ServerThreadFunc(int port);

	// Helper: Get metrics snapshot (thread-safe)
	PerformanceMetrics GetMetricsSnapshot() const;
};

} // namespace Foundation
