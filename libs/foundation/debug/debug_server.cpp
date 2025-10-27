// HTTP Debug Server implementation using cpp-httplib.

#include "debug/debug_server.h"
#include <httplib.h>
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>

namespace Foundation {

DebugServer::DebugServer() : m_running(false) {
	// Lock-free ring buffer is initialized automatically
}

DebugServer::~DebugServer() {
	Stop();
}

void DebugServer::Start(int port) {
	if (m_running.load()) {
		std::cerr << "DebugServer already running!" << std::endl;
		return;
	}

	m_running.store(true);

	// Start server thread
	m_serverThread = std::thread(&DebugServer::ServerThreadFunc, this, port);

	std::cout << "Debug server starting on port " << port << "..." << std::endl;
}

void DebugServer::Stop() {
	if (!m_running.load()) {
		return;
	}

	m_running.store(false);

	// Stop the server
	if (m_server) {
		m_server->stop();
	}

	// Wait for server thread to finish
	if (m_serverThread.joinable()) {
		m_serverThread.join();
	}

	std::cout << "Debug server stopped" << std::endl;
}

void DebugServer::UpdateMetrics(const PerformanceMetrics& metrics) {
	// Lock-free write - never blocks, ~10-20 nanoseconds
	m_metricsBuffer.Write(metrics);
}

PerformanceMetrics DebugServer::GetMetricsSnapshot() const {
	// Lock-free read - gets latest sample, ~10-20 nanoseconds
	PerformanceMetrics metrics = {};
	m_metricsBuffer.ReadLatest(metrics);
	return metrics;
}

void DebugServer::ServerThreadFunc(int port) {
	m_server = std::make_unique<httplib::Server>();

	// --- REST Endpoints ---

	// Health check endpoint
	m_server->Get("/api/health", [this](const httplib::Request&, httplib::Response& res) {
		PerformanceMetrics metrics = GetMetricsSnapshot();

		std::ostringstream json;
		json << "{";
		json << "\"status\":\"ok\",";
		json << "\"uptime\":" << metrics.timestamp;
		json << "}";

		res.set_content(json.str(), "application/json");
		res.set_header("Access-Control-Allow-Origin", "*");
	});

	// Current metrics snapshot
	m_server->Get("/api/metrics", [this](const httplib::Request&, httplib::Response& res) {
		PerformanceMetrics metrics = GetMetricsSnapshot();
		res.set_content(metrics.ToJSON(), "application/json");
		res.set_header("Access-Control-Allow-Origin", "*");
	});

	// --- SSE Streaming Endpoint ---

	// Real-time metrics stream (10 Hz)
	m_server->Get("/stream/metrics", [this](const httplib::Request&, httplib::Response& res) {
		res.set_header("Content-Type", "text/event-stream");
		res.set_header("Cache-Control", "no-cache");
		res.set_header("Connection", "keep-alive");
		res.set_header("Access-Control-Allow-Origin", "*");

		res.set_chunked_content_provider(
			"text/event-stream",
			[this](size_t /*offset*/, httplib::DataSink& sink) {
				const int updateRateHz = 10;
				const auto updateInterval = std::chrono::milliseconds(1000 / updateRateHz);
				auto lastUpdate = std::chrono::steady_clock::now();

				while (m_running.load() && sink.is_writable()) {
					auto now = std::chrono::steady_clock::now();
					auto elapsed = now - lastUpdate;

					// Send update if enough time has passed
					if (elapsed >= updateInterval) {
						PerformanceMetrics metrics = GetMetricsSnapshot();

						std::ostringstream event;
						event << "event: metric\n";
						event << "data: " << metrics.ToJSON() << "\n\n";

						std::string eventStr = event.str();
						if (!sink.write(eventStr.c_str(), eventStr.size())) {
							break; // Client disconnected
						}

						lastUpdate = now;
					}

					// Sleep briefly to avoid busy-waiting
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}

				return false; // Done streaming
			}
		);
	});

	// Simple HTML page for manual browser testing
	m_server->Get("/", [](const httplib::Request&, httplib::Response& res) {
		const char* html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>ui-sandbox Debug</title>
    <style>
        body { font-family: monospace; background: #1e1e1e; color: #d4d4d4; padding: 20px; }
        h1 { color: #4ec9b0; }
        .metric { margin: 10px 0; padding: 10px; background: #252526; border-left: 3px solid #007acc; }
        .metric-name { color: #9cdcfe; }
        .metric-value { color: #ce9178; font-size: 1.2em; }
    </style>
</head>
<body>
    <h1>ui-sandbox Performance Metrics</h1>
    <div id="metrics">
        <div class="metric">
            <span class="metric-name">FPS:</span>
            <span class="metric-value" id="fps">--</span>
        </div>
        <div class="metric">
            <span class="metric-name">Frame Time:</span>
            <span class="metric-value" id="frameTime">--</span> ms
        </div>
        <div class="metric">
            <span class="metric-name">Frame Range:</span>
            <span class="metric-value" id="frameRange">--</span> ms
        </div>
        <div class="metric">
            <span class="metric-name">Draw Calls:</span>
            <span class="metric-value" id="drawCalls">--</span>
        </div>
        <div class="metric">
            <span class="metric-name">Vertices:</span>
            <span class="metric-value" id="vertices">--</span>
        </div>
        <div class="metric">
            <span class="metric-name">Triangles:</span>
            <span class="metric-value" id="triangles">--</span>
        </div>
    </div>
    <script>
        const source = new EventSource('/stream/metrics');
        source.addEventListener('metric', (e) => {
            const data = JSON.parse(e.data);
            document.getElementById('fps').textContent = data.fps.toFixed(1);
            document.getElementById('frameTime').textContent = data.frameTimeMs.toFixed(2);
            document.getElementById('frameRange').textContent =
                data.frameTimeMinMs.toFixed(2) + ' - ' + data.frameTimeMaxMs.toFixed(2);
            document.getElementById('drawCalls').textContent = data.drawCalls;
            document.getElementById('vertices').textContent = data.vertexCount;
            document.getElementById('triangles').textContent = data.triangleCount;
        });
        source.addEventListener('error', () => {
            console.error('SSE connection error');
        });
    </script>
</body>
</html>
)";
		res.set_content(html, "text/html");
	});

	// Start listening
	std::cout << "Debug server listening on http://localhost:" << port << std::endl;
	m_server->listen("127.0.0.1", port);
}

} // namespace Foundation
