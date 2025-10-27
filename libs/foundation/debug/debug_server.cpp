// HTTP Debug Server implementation using cpp-httplib.

#include "debug/debug_server.h"
#include <httplib.h>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iostream>
#include <sstream>
#include <thread>

namespace Foundation {

// Helper functions for LogEntry
static const char* LogLevelToString(LogLevel level) {
	switch (level) {
		case LogLevel::Debug: return "DEBUG";
		case LogLevel::Info: return "INFO";
		case LogLevel::Warning: return "WARN";
		case LogLevel::Error: return "ERROR";
		default: return "UNKNOWN";
	}
}

static const char* LogCategoryToString(LogCategory category) {
	switch (category) {
		case LogCategory::Renderer: return "Renderer";
		case LogCategory::Physics: return "Physics";
		case LogCategory::Audio: return "Audio";
		case LogCategory::Network: return "Network";
		case LogCategory::Game: return "Game";
		case LogCategory::World: return "World";
		case LogCategory::UI: return "UI";
		case LogCategory::Engine: return "Engine";
		case LogCategory::Foundation: return "Foundation";
		default: return "Unknown";
	}
}

// Global buffer for LogEntry::ToJSON() (thread_local to be thread-safe)
static thread_local char g_logJsonBuffer[512];

const char* LogEntry::ToJSON() const {
	std::snprintf(
		g_logJsonBuffer,
		sizeof(g_logJsonBuffer),
		R"({"level":"%s","category":"%s","message":"%s","timestamp":%llu,"file":"%s","line":%d})",
		LogLevelToString(level),
		LogCategoryToString(category),
		message,
		static_cast<unsigned long long>(timestamp),
		file ? file : "",
		line
	);
	return g_logJsonBuffer;
}

DebugServer::DebugServer() : m_running(false) {
	// Lock-free ring buffers are initialized automatically
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

void DebugServer::UpdateLog(LogLevel level, LogCategory category, const char* message, const char* file, int line) {
	// Get current timestamp in milliseconds
	auto now = std::chrono::system_clock::now();
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

	// Create log entry
	LogEntry entry;
	entry.level = level;
	entry.category = category;
	entry.timestamp = static_cast<uint64_t>(ms);
	entry.file = file;  // Pointer to static string (safe)
	entry.line = line;

	// Copy message (truncate if too long)
	std::strncpy(entry.message, message, sizeof(entry.message) - 1);
	entry.message[sizeof(entry.message) - 1] = '\0';  // Ensure null termination

	// Lock-free write - never blocks, ~10-20 nanoseconds
	// If buffer is full, oldest entry is silently dropped (circular buffer)
	m_logBuffer.Write(entry);
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

	// Real-time log stream (10 Hz, throttled)
	m_server->Get("/stream/logs", [this](const httplib::Request&, httplib::Response& res) {
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

				// Track which logs we've already sent
				uint64_t lastSentTimestamp = 0;

				while (m_running.load() && sink.is_writable()) {
					auto now = std::chrono::steady_clock::now();
					auto elapsed = now - lastUpdate;

					// Send updates at throttled rate
					if (elapsed >= updateInterval) {
						// Read all available log entries from ring buffer
						LogEntry entry;
						while (m_logBuffer.Read(entry)) {
							// Only send logs we haven't sent yet
							if (entry.timestamp > lastSentTimestamp) {
								std::ostringstream event;
								event << "event: log\n";
								event << "data: " << entry.ToJSON() << "\n\n";

								std::string eventStr = event.str();
								if (!sink.write(eventStr.c_str(), eventStr.size())) {
									return false; // Client disconnected
								}

								lastSentTimestamp = entry.timestamp;
							}
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
		const char* html = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <title>ui-sandbox Debug</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { font-family: 'Consolas', 'Monaco', monospace; background: #1e1e1e; color: #d4d4d4; }

        header { background: #252526; padding: 20px; border-bottom: 1px solid #007acc; }
        h1 { color: #4ec9b0; margin-bottom: 15px; }

        /* Tab Navigation */
        .tabs { display: flex; gap: 5px; }
        .tab {
            padding: 8px 20px;
            background: #333333;
            border: none;
            color: #d4d4d4;
            cursor: pointer;
            font-family: inherit;
            font-size: 14px;
            border-radius: 4px 4px 0 0;
        }
        .tab:hover { background: #3e3e3e; }
        .tab.active { background: #007acc; color: #ffffff; }

        /* Tab Content */
        .tab-content { display: none; padding: 20px; height: calc(100vh - 140px); overflow-y: auto; }
        .tab-content.active { display: block; }

        /* Performance Metrics */
        .metric {
            margin: 10px 0;
            padding: 15px;
            background: #252526;
            border-left: 3px solid #007acc;
            display: flex;
            justify-content: space-between;
        }
        .metric-name { color: #9cdcfe; }
        .metric-value { color: #ce9178; font-size: 1.2em; font-weight: bold; }

        /* Logs */
        .log-container {
            background: #1e1e1e;
            height: 100%;
            overflow-y: auto;
            font-size: 13px;
            line-height: 1.5;
        }
        .log-entry {
            padding: 4px 10px;
            border-left: 3px solid transparent;
            white-space: pre-wrap;
            word-break: break-word;
        }
        .log-entry:hover { background: #2d2d30; }

        /* Log Level Colors */
        .log-DEBUG { border-left-color: #808080; color: #808080; }
        .log-INFO { border-left-color: #d4d4d4; color: #d4d4d4; }
        .log-WARN { border-left-color: #dcdcaa; color: #dcdcaa; }
        .log-ERROR { border-left-color: #f48771; color: #f48771; }

        .log-timestamp { color: #858585; margin-right: 8px; }
        .log-category { color: #9cdcfe; margin-right: 8px; font-weight: bold; }
        .log-level { margin-right: 8px; font-weight: bold; }
        .log-file { color: #858585; font-size: 11px; margin-left: 8px; }
    </style>
</head>
<body>
    <header>
        <h1>ui-sandbox Debug</h1>
        <div class="tabs">
            <button class="tab active" onclick="switchTab('performance')">Performance</button>
            <button class="tab" onclick="switchTab('logs')">Logs</button>
        </div>
    </header>

    <!-- Performance Tab -->
    <div id="performance-tab" class="tab-content active">
        <div class="metric">
            <span class="metric-name">FPS:</span>
            <span class="metric-value" id="fps">--</span>
        </div>
        <div class="metric">
            <span class="metric-name">Frame Time:</span>
            <span class="metric-value"><span id="frameTime">--</span> ms</span>
        </div>
        <div class="metric">
            <span class="metric-name">Frame Range:</span>
            <span class="metric-value" id="frameRange">--</span>
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

    <!-- Logs Tab -->
    <div id="logs-tab" class="tab-content">
        <div id="logs" class="log-container"></div>
    </div>

    <script>
        // Tab switching
        function switchTab(tabName) {
            // Update tab buttons
            document.querySelectorAll('.tab').forEach(tab => tab.classList.remove('active'));
            event.target.classList.add('active');

            // Update tab content
            document.querySelectorAll('.tab-content').forEach(content => content.classList.remove('active'));
            document.getElementById(tabName + '-tab').classList.add('active');
        }

        // Performance Metrics Streaming
        const metricsSource = new EventSource('/stream/metrics');
        metricsSource.addEventListener('metric', (e) => {
            const data = JSON.parse(e.data);
            document.getElementById('fps').textContent = data.fps.toFixed(1);
            document.getElementById('frameTime').textContent = data.frameTimeMs.toFixed(2);
            document.getElementById('frameRange').textContent =
                data.frameTimeMinMs.toFixed(2) + ' - ' + data.frameTimeMaxMs.toFixed(2);
            document.getElementById('drawCalls').textContent = data.drawCalls;
            document.getElementById('vertices').textContent = data.vertexCount;
            document.getElementById('triangles').textContent = data.triangleCount;
        });
        metricsSource.addEventListener('error', () => {
            console.error('Metrics SSE connection error');
        });

        // Log Streaming
        const logsContainer = document.getElementById('logs');
        const maxLogs = 500; // Limit to prevent memory issues
        let logCount = 0;

        const logsSource = new EventSource('/stream/logs');
        logsSource.addEventListener('log', (e) => {
            const log = JSON.parse(e.data);

            // Create log entry element
            const entry = document.createElement('div');
            entry.className = 'log-entry log-' + log.level;

            // Format timestamp (convert from ms to readable time)
            const date = new Date(log.timestamp);
            const time = date.toLocaleTimeString('en-US', { hour12: false });

            // Build log line
            let logLine = '';
            logLine += '<span class="log-timestamp">[' + time + ']</span>';
            logLine += '<span class="log-category">[' + log.category + ']</span>';
            logLine += '<span class="log-level">[' + log.level + ']</span>';
            logLine += '<span class="log-message">' + escapeHtml(log.message) + '</span>';

            // Add file:line for warnings and errors
            if (log.level === 'WARN' || log.level === 'ERROR') {
                logLine += '<span class="log-file">(' + log.file + ':' + log.line + ')</span>';
            }

            entry.innerHTML = logLine;
            logsContainer.appendChild(entry);
            logCount++;

            // Remove old logs if we exceed the limit
            if (logCount > maxLogs) {
                logsContainer.removeChild(logsContainer.firstChild);
                logCount--;
            }

            // Auto-scroll to bottom
            logsContainer.scrollTop = logsContainer.scrollHeight;
        });
        logsSource.addEventListener('error', () => {
            console.error('Logs SSE connection error');
        });

        // Helper: Escape HTML to prevent XSS
        function escapeHtml(text) {
            const div = document.createElement('div');
            div.textContent = text;
            return div.innerHTML;
        }
    </script>
</body>
</html>
)HTML";
		res.set_content(html, "text/html");
	});

	// Start listening
	std::cout << "Debug server listening on http://localhost:" << port << std::endl;
	m_server->listen("127.0.0.1", port);
}

} // namespace Foundation
