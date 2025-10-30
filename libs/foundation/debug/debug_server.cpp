// HTTP Debug Server implementation using cpp-httplib.

#include "debug/debug_server.h"
#include "utils/log.h"
#include <chrono>
#include <cstring>
#include <ctime>
#include <httplib.h>
#include <iostream>
#include <sstream>
#include <thread>

// OpenGL for framebuffer capture
#include <GL/glew.h>

// stb_image_write for PNG encoding
// Protect against multiple definition errors if this gets included elsewhere
#ifndef WORLDSIM_STB_IMAGE_WRITE_IMPL
#define WORLDSIM_STB_IMAGE_WRITE_IMPL
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#endif

namespace Foundation {

	// Helper functions for LogEntry
	static const char* LogLevelToString(LogLevel level) {
		switch (level) {
			case LogLevel::Debug:
				return "DEBUG";
			case LogLevel::Info:
				return "INFO";
			case LogLevel::Warning:
				return "WARN";
			case LogLevel::Error:
				return "ERROR";
			default:
				return "UNKNOWN";
		}
	}

	static const char* LogCategoryToString(LogCategory category) {
		switch (category) {
			case LogCategory::Renderer:
				return "Renderer";
			case LogCategory::Physics:
				return "Physics";
			case LogCategory::Audio:
				return "Audio";
			case LogCategory::Network:
				return "Network";
			case LogCategory::Game:
				return "Game";
			case LogCategory::World:
				return "World";
			case LogCategory::UI:
				return "UI";
			case LogCategory::Engine:
				return "Engine";
			case LogCategory::Foundation:
				return "Foundation";
			default:
				return "Unknown";
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

	DebugServer::DebugServer()
		: m_running(false) {
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
		entry.file = file; // Pointer to static string (safe)
		entry.line = line;

		// Copy message (truncate if too long)
		std::strncpy(entry.message, message, sizeof(entry.message) - 1);
		entry.message[sizeof(entry.message) - 1] = '\0'; // Ensure null termination

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

	void DebugServer::CaptureScreenshotIfRequested() {
		// Check if screenshot was requested (non-blocking check)
		if (!m_screenshotRequested.load()) {
			return; // No screenshot requested
		}

		LOG_INFO(Foundation, "Screenshot requested, beginning capture...");

		// Get current framebuffer size
		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);
		int width = viewport[2];
		int height = viewport[3];

		if (width <= 0 || height <= 0) {
			LOG_ERROR(Foundation, "Invalid viewport size for screenshot: %dx%d", width, height);
			m_screenshotRequested.store(false);
			return;
		}

		LOG_DEBUG(Foundation, "Capturing screenshot: %dx%d", width, height);

		// Allocate buffer for pixel data (RGBA, 4 bytes per pixel - more efficient than RGB on most hardware)
		std::vector<unsigned char> pixels(width * height * 4);

		// Read pixels from framebuffer
		glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

		// Flip image vertically (OpenGL origin is bottom-left, PNG origin is top-left)
		std::vector<unsigned char> flipped(width * height * 4);
		for (int y = 0; y < height; y++) {
			memcpy(flipped.data() + (height - 1 - y) * width * 4, pixels.data() + y * width * 4, width * 4);
		}

		// Encode to PNG using stb_image_write
		// We use a custom write function to write to a vector instead of a file
		struct PNGWriteContext {
			std::vector<unsigned char>* data;
		};

		PNGWriteContext context;
		context.data = &m_screenshotData;

		// Write callback for stb_image_write
		auto pngWriteFunc = [](void* context, void* data, int size) {
			PNGWriteContext* ctx = static_cast<PNGWriteContext*>(context);
			unsigned char*	 bytes = static_cast<unsigned char*>(data);
			ctx->data->insert(ctx->data->end(), bytes, bytes + size);
		};

		// Encode to PNG (hold mutex for entire operation to prevent race conditions)
		LOG_DEBUG(Foundation, "Encoding screenshot to PNG...");
		int result;
		{
			std::lock_guard<std::mutex> lock(m_screenshotMutex);
			m_screenshotData.clear();

			result = stbi_write_png_to_func(
				pngWriteFunc,
				&context,
				width,
				height,
				4, // RGBA (4 components)
				flipped.data(),
				width * 4 // stride
			);
		}

		if (result == 0) {
			LOG_ERROR(Foundation, "Failed to encode screenshot to PNG");
			m_screenshotRequested.store(false);
			return;
		}

		LOG_INFO(Foundation, "Screenshot captured successfully (%zu bytes)", m_screenshotData.size());

		// Signal that screenshot is ready
		m_screenshotReady.store(true);
		m_screenshotRequested.store(false);
	}

	std::string DebugServer::GetTargetSceneName() const {
		std::lock_guard<std::mutex> lock(m_sceneNameMutex);
		return m_targetSceneName;
	}

	bool DebugServer::RequestScreenshot(std::vector<unsigned char>& pngData, int timeoutMs) {
		LOG_INFO(Foundation, "Screenshot requested via HTTP, waiting for capture...");

		// Clear any previous ready state
		m_screenshotReady.store(false);

		// Request screenshot
		m_screenshotRequested.store(true);

		// Wait for screenshot to be ready (with timeout)
		auto startTime = std::chrono::steady_clock::now();
		while (!m_screenshotReady.load()) {
			auto elapsed = std::chrono::steady_clock::now() - startTime;
			if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeoutMs) {
				// Timeout - cancel request
				LOG_ERROR(Foundation, "Screenshot capture timeout after %dms", timeoutMs);
				m_screenshotRequested.store(false);
				return false;
			}

			// Sleep briefly to avoid busy-waiting
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		// Copy screenshot data
		{
			std::lock_guard<std::mutex> lock(m_screenshotMutex);
			pngData = m_screenshotData;
		}

		LOG_INFO(Foundation, "Screenshot data copied to HTTP response (%zu bytes)", pngData.size());

		// Clear ready flag
		m_screenshotReady.store(false);

		return true;
	}

	void DebugServer::ServerThreadFunc(int port) {
		m_server = std::make_unique<httplib::Server>();

		// Disable SO_REUSEADDR to prevent multiple instances on same port
		m_server->set_socket_options([](socket_t sock) {
			int reuse = 0;
			setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
		});

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

		// Screenshot endpoint
		m_server->Get("/api/ui/screenshot", [this](const httplib::Request&, httplib::Response& res) {
			std::vector<unsigned char> pngData;

			// Request screenshot and wait for it (10 second timeout for large screenshots)
			if (RequestScreenshot(pngData, 10000)) {
				// Screenshot captured successfully - avoid copy by using data() and size()
				res.set_content(reinterpret_cast<const char*>(pngData.data()), pngData.size(), "image/png");
				res.set_header("Access-Control-Allow-Origin", "*");
				res.set_header("Content-Disposition", "inline; filename=\"screenshot.png\"");
			} else {
				// Timeout or error
				res.status = 500;
				res.set_content("{\"error\":\"Screenshot capture timeout or failed\"}", "application/json");
				res.set_header("Access-Control-Allow-Origin", "*");
			}
		});

		// Control endpoint - allows control of sandbox via HTTP GET with query params
		// Examples: /api/control?action=exit
		//           /api/control?action=scene&scene=arena
		//           /api/control?action=pause
		m_server->Get("/api/control", [this](const httplib::Request& req, httplib::Response& res) {
			res.set_header("Access-Control-Allow-Origin", "*");

			// Get action parameter (required)
			if (!req.has_param("action")) {
				res.status = 400;
				res.set_content("{\"error\":\"Missing required parameter 'action'\"}", "application/json");
				return;
			}

			std::string action = req.get_param_value("action");

			// Process action
			if (action == "exit") {
				m_controlAction.store(ControlAction::Exit);
				res.set_content("{\"status\":\"ok\",\"action\":\"exit\"}", "application/json");
			} else if (action == "scene") {
				// Scene change requires 'scene' parameter
				if (!req.has_param("scene")) {
					res.status = 400;
					res.set_content("{\"error\":\"Scene change requires 'scene' parameter\"}", "application/json");
					return;
				}

				std::string sceneName = req.get_param_value("scene");
				{
					std::lock_guard<std::mutex> lock(m_sceneNameMutex);
					m_targetSceneName = sceneName;
					m_controlAction.store(ControlAction::SceneChange);
				}

				std::ostringstream json;
				json << "{\"status\":\"ok\",\"action\":\"scene\",\"scene\":\"" << sceneName << "\"}";
				res.set_content(json.str(), "application/json");
			} else if (action == "pause") {
				m_controlAction.store(ControlAction::Pause);
				res.set_content("{\"status\":\"ok\",\"action\":\"pause\"}", "application/json");
			} else if (action == "resume") {
				m_controlAction.store(ControlAction::Resume);
				res.set_content("{\"status\":\"ok\",\"action\":\"resume\"}", "application/json");
			} else if (action == "reload") {
				m_controlAction.store(ControlAction::ReloadScene);
				res.set_content("{\"status\":\"ok\",\"action\":\"reload\"}", "application/json");
			} else {
				res.status = 400;
				std::ostringstream json;
				json << "{\"error\":\"Invalid action '" << action << "'. Valid actions: exit, scene, pause, resume, reload\"}";
				res.set_content(json.str(), "application/json");
			}
		});

		// --- SSE Streaming Endpoint ---

		// Real-time metrics stream (10 Hz)
		m_server->Get("/stream/metrics", [this](const httplib::Request&, httplib::Response& res) {
			res.set_header("Content-Type", "text/event-stream");
			res.set_header("Cache-Control", "no-cache");
			res.set_header("Connection", "keep-alive");
			res.set_header("Access-Control-Allow-Origin", "*");

			res.set_chunked_content_provider("text/event-stream", [this](size_t /*offset*/, httplib::DataSink& sink) {
				const int  updateRateHz = 10;
				const auto updateInterval = std::chrono::milliseconds(1000 / updateRateHz);
				auto	   lastUpdate = std::chrono::steady_clock::now();

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
			});
		});

		// Real-time log stream (10 Hz, throttled)
		m_server->Get("/stream/logs", [this](const httplib::Request&, httplib::Response& res) {
			res.set_header("Content-Type", "text/event-stream");
			res.set_header("Cache-Control", "no-cache");
			res.set_header("Connection", "keep-alive");
			res.set_header("Access-Control-Allow-Origin", "*");

			res.set_chunked_content_provider("text/event-stream", [this](size_t /*offset*/, httplib::DataSink& sink) {
				const int  updateRateHz = 10;
				const auto updateInterval = std::chrono::milliseconds(1000 / updateRateHz);
				auto	   lastUpdate = std::chrono::steady_clock::now();

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
							// Use >= to handle multiple logs with same timestamp
							if (entry.timestamp >= lastSentTimestamp) {
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
			});
		});

		// Serve static developer client from build directory
		// NOTE: The developer-client is a React SPA built by Vite and copied to
		// build/developer-client/ by CMake. The files use relative paths and can
		// be opened directly in a browser, or served here for convenience.
		//
		// To open directly: open build/developer-client/index.html
		// To access via server: http://localhost:<port>/

		// TODO: Implement static file serving when developer-client is built
		// For now, return a simple placeholder that explains how to access the app
		m_server->Get("/", [](const httplib::Request&, httplib::Response& res) {
			const char* html = R"HTML(<!DOCTYPE html>
<html>
<head>
    <title>Developer Server</title>
    <style>
        body {
            font-family: 'Consolas', 'Monaco', monospace;
            background: #1e1e1e;
            color: #d4d4d4;
            padding: 40px;
            max-width: 800px;
            margin: 0 auto;
        }
        h1 { color: #4ec9b0; }
        .info { background: #252526; padding: 20px; margin: 20px 0; border-left: 3px solid #007acc; }
        code { background: #1e1e1e; padding: 2px 6px; color: #ce9178; }
        a { color: #4ec9b0; }
    </style>
</head>
<body>
    <h1>Developer Server Running</h1>

    <div class="info">
        <h2>API Endpoints Available:</h2>
        <ul>
            <li><a href="/api/health">/api/health</a> - Server health check</li>
            <li><a href="/api/metrics">/api/metrics</a> - Current performance metrics</li>
            <li><a href="/api/ui/screenshot">/api/ui/screenshot</a> - Capture screenshot (PNG)</li>
            <li><a href="/stream/metrics">/stream/metrics</a> - Real-time metrics (SSE)</li>
            <li><a href="/stream/logs">/stream/logs</a> - Real-time logs (SSE)</li>
        </ul>
    </div>

    <div class="info">
        <h2>Developer Client (React SPA):</h2>
        <p>The developer client is a React application that connects to this server.</p>
        <p><strong>To launch:</strong></p>
        <ul>
            <li>Build the project: <code>make</code></li>
            <li>Open: <code>open build/developer-client/index.html</code></li>
        </ul>
        <p>The app will connect to this server and display real-time metrics and logs.</p>
    </div>
</body>
</html>
)HTML";
			res.set_content(html, "text/html");
		});

		// Start listening (this will fail if port is in use since SO_REUSEADDR is disabled)
		std::cout << "Debug server listening on http://localhost:" << port << std::endl;
		if (!m_server->listen("127.0.0.1", port)) {
			std::cerr << "\n";
			std::cerr << "ERROR: Port " << port << " is already in use.\n";
			std::cerr << "An instance of the sandbox is already running.\n";
			std::cerr << "Use the following command to kill it:\n";
			std::cerr << "  curl http://127.0.0.1:" << port << "/api/control?action=exit\n";
			std::cerr << "\n";
			m_running.store(false);
			std::exit(1);
		}
	}

} // namespace Foundation
