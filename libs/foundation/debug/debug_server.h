#pragma once

// HTTP Debug Server - Serves performance metrics via HTTP/SSE.
//
// This server runs on a separate thread and provides:
// - REST endpoints for current metrics snapshots
// - Server-Sent Events for real-time metric streaming
//
// Lock-free design: Game thread writes to ring buffer (never blocks),
// HTTP thread reads latest sample. Zero mutex contention.

#include "metrics/performance_metrics.h"
#include "debug/lock_free_ring_buffer.h"
#include <atomic>
#include <memory>
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

	// Lock-free metrics buffer (game thread writes, HTTP thread reads)
	LockFreeRingBuffer<PerformanceMetrics, 64> m_metricsBuffer;

	// Server thread entry point
	void ServerThreadFunc(int port);

	// Helper: Get metrics snapshot (lock-free read)
	PerformanceMetrics GetMetricsSnapshot() const;
};

} // namespace Foundation
