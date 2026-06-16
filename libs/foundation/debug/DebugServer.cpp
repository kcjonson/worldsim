// HTTP Debug Server implementation using cpp-httplib.

#include "debug/DebugServer.h"
#include "utils/Log.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <httplib.h>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <thread>

// OpenGL for framebuffer capture
#include <GL/glew.h>

// PNG encoding is centralized in Foundation::PngEncoder (single STB definition site).
#include "graphics/PngEncoder.h"

namespace Foundation {

	// Parse one /api/input 'ev' value: "type,x,y[,button][,delta]" (see endpoint
	// comment). Returns false with a message in error on malformed input.
	static bool parseInputEvent(const std::string& ev, InputCommand& cmd, std::string& error) {
		std::vector<std::string> fields;
		std::stringstream ss(ev);
		std::string field;
		while (std::getline(ss, field, ',')) {
			fields.push_back(field);
		}

		if (fields.empty()) {
			error = "empty event";
			return false;
		}

		const std::string& type = fields[0];
		if (type == "move")
			cmd.type = InputCommand::Type::Move;
		else if (type == "down")
			cmd.type = InputCommand::Type::Down;
		else if (type == "up")
			cmd.type = InputCommand::Type::Up;
		else if (type == "click")
			cmd.type = InputCommand::Type::Click;
		else if (type == "scroll")
			cmd.type = InputCommand::Type::Scroll;
		else if (type == "keydown")
			cmd.type = InputCommand::Type::KeyDown;
		else if (type == "keyup")
			cmd.type = InputCommand::Type::KeyUp;
		else {
			error = "unknown event type (expected move|down|up|click|scroll|keydown|keyup)";
			return false;
		}

		// Key events carry a key name instead of coordinates: "keydown,R" / "keyup,Escape".
		if (cmd.type == InputCommand::Type::KeyDown || cmd.type == InputCommand::Type::KeyUp) {
			if (fields.size() < 2 || fields[1].empty()) {
				error = "key event requires a key name (e.g. keydown,R)";
				return false;
			}
			cmd.keyName = fields[1];
			return true;
		}

		if (fields.size() < 3) {
			error = "event requires x,y coordinates";
			return false;
		}
		try {
			cmd.x = std::stof(fields[1]);
			cmd.y = std::stof(fields[2]);
		} catch (...) {
			error = "non-numeric coordinates";
			return false;
		}

		if (cmd.type == InputCommand::Type::Scroll) {
			if (fields.size() < 4) {
				error = "scroll requires a delta";
				return false;
			}
			try {
				cmd.scrollDelta = std::stof(fields[3]);
			} catch (...) {
				error = "non-numeric scroll delta";
				return false;
			}
		} else if (fields.size() >= 4) {
			const std::string& button = fields[3];
			if (button == "left")        cmd.button = 0;
			else if (button == "right")  cmd.button = 1;
			else if (button == "middle") cmd.button = 2;
			else {
				error = "unknown button (expected left|right|middle)";
				return false;
			}
		}

		return true;
	}

	std::string parseDevVerb(const std::string& path) {
		// Expect "/api/dev/<verb>"; the verb is the segment after the prefix. A
		// trailing slash or extra path is trimmed at the next '/'.
		constexpr std::string_view kPrefix = "/api/dev/";
		if (path.size() <= kPrefix.size() || path.compare(0, kPrefix.size(), kPrefix) != 0) {
			return {};
		}
		std::string verb = path.substr(kPrefix.size());
		if (auto slash = verb.find('/'); slash != std::string::npos) {
			verb = verb.substr(0, slash);
		}
		std::transform(verb.begin(), verb.end(), verb.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return verb;
	}

	// Helper function to escape strings for JSON
	static std::string escapeJsonString(const std::string& str) {
		std::string escaped;
		escaped.reserve(str.length());
		for (char c : str) {
			switch (c) {
				case '"':
					escaped += "\\\"";
					break;
				case '\\':
					escaped += "\\\\";
					break;
				case '\b':
					escaped += "\\b";
					break;
				case '\f':
					escaped += "\\f";
					break;
				case '\n':
					escaped += "\\n";
					break;
				case '\r':
					escaped += "\\r";
					break;
				case '\t':
					escaped += "\\t";
					break;
				default:
					escaped += c;
			}
		}
		return escaped;
	}

	// Helper functions for LogEntry
	static const char* logLevelToString(LogLevel level) {
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

	static const char* logCategoryToString(LogCategory category) {
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

	// Global buffer for LogEntry::toJSON() (thread_local to be thread-safe)
	static thread_local char g_logJsonBuffer[512];

	const char* LogEntry::toJSON() const { // NOLINT(readability-convert-member-functions-to-static)
		std::snprintf(
			g_logJsonBuffer,
			sizeof(g_logJsonBuffer),
			R"({"level":"%s","category":"%s","message":"%s","timestamp":%llu,"file":"%s","line":%d})",
			logLevelToString(level),
			logCategoryToString(category),
			message,
			static_cast<unsigned long long>(timestamp),
			(file != nullptr) ? file : "",
			line
		);
		return g_logJsonBuffer;
	}

	DebugServer::DebugServer() // NOLINT(cppcoreguidelines-pro-type-member-init,modernize-use-equals-default)
		: server(),
		  serverThread(),
		  running(false),
		  screenshotData(),
		  screenshotMutex(),
		  targetSceneName(),
		  sceneNameMutex() {
		// Lock-free ring buffers are initialized automatically
	}

	DebugServer::~DebugServer() {
		stop();
	}

	void DebugServer::start(int port) { // NOLINT(readability-convert-member-functions-to-static)
		if (running.load()) {
			std::cerr << "DebugServer already running!" << std::endl;
			return;
		}

		// Reset shutdown synchronization state for restart scenarios
		{
			std::lock_guard<std::mutex> lock(shutdownMutex);
			shutdownComplete = false;
			handlerDone = false;
		}
		controlAction.store(ControlAction::None);

		running.store(true);

		// Start server thread
		serverThread = std::thread(&DebugServer::serverThreadFunc, this, port);

		std::cout << "Debug server starting on port " << port << "..." << std::endl;
	}

	void DebugServer::stop() { // NOLINT(readability-convert-member-functions-to-static)
		if (!running.load()) {
			return;
		}

		running.store(false);

		// If an exit was triggered via HTTP, wait for the handler to set the response
		// Check exitTriggered and handlerDone inside the lock to avoid race condition
		{
			std::unique_lock<std::mutex> lock(shutdownMutex);
			bool						 exitTriggered = (controlAction.load() == ControlAction::Exit);
			if (exitTriggered && !handlerDone) {
				shutdownCV.wait(lock, [this] { return handlerDone; });
			}
			// Handler has set response and is about to return (or already returned)
		}

		// Stop the server - this will:
		// 1. Complete sending any pending responses
		// 2. Close the listening socket
		// 3. Make listen() return
		if (server) {
			server->stop();
		}

		// Wait for server thread to finish
		if (serverThread.joinable()) {
			serverThread.join();
		}

		std::cout << "Debug server stopped" << std::endl;
	}

	void DebugServer::updateMetrics(const PerformanceMetrics& metrics) {
		// Lock-free write - never blocks, ~10-20 nanoseconds
		metricsBuffer.write(metrics);
	}

	void DebugServer::updateLog( // NOLINT(readability-convert-member-functions-to-static)
		LogLevel	level,
		LogCategory category,
		const char* message,
		const char* file,
		int			line
	) { // NOLINT(readability-convert-member-functions-to-static)
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
		logBuffer.write(entry);
	}

	PerformanceMetrics DebugServer::getMetricsSnapshot() const { // NOLINT(readability-convert-member-functions-to-static)
		// Lock-free read - gets latest sample, ~10-20 nanoseconds
		PerformanceMetrics metrics = {};
		metricsBuffer.readLatest(metrics);
		return metrics;
	}

	void DebugServer::captureScreenshotIfRequested() { // NOLINT(readability-convert-member-functions-to-static)
		// Check if screenshot was requested (non-blocking check)
		if (!screenshotRequested.load()) {
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
			screenshotRequested.store(false);
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

		// Encode to PNG via the shared encoder (single STB definition site).
		LOG_DEBUG(Foundation, "Encoding screenshot to PNG...");
		bool encodeFailed = false;
		{
			std::lock_guard<std::mutex> lock(screenshotMutex);
			screenshotData = encodePngToMemory(flipped.data(), width, height);
			encodeFailed = screenshotData.empty();
		}

		if (encodeFailed) {
			LOG_ERROR(Foundation, "Failed to encode screenshot to PNG");
			screenshotRequested.store(false);
			return;
		}

		LOG_INFO(Foundation, "Screenshot captured successfully (%zu bytes)", screenshotData.size());

		// Signal that screenshot is ready
		screenshotReady.store(true);
		screenshotRequested.store(false);
	}

	std::string DebugServer::getTargetSceneName() const {
		std::lock_guard<std::mutex> lock(sceneNameMutex);
		return targetSceneName;
	}

	bool DebugServer::consumeCameraCommand(CameraCommand& out) {
		if (!cameraCommandPending.load()) {
			return false;
		}
		std::lock_guard<std::mutex> lock(cameraCommandMutex);
		out = cameraCommand;
		cameraCommandPending.store(false);
		return true;
	}

	bool DebugServer::consumeInputCommands(std::vector<InputCommand>& out) {
		if (!inputCommandsPending.load()) {
			return false;
		}
		std::lock_guard<std::mutex> lock(inputCommandsMutex);
		out.insert(out.end(), inputCommands.begin(), inputCommands.end());
		inputCommands.clear();
		inputCommandsPending.store(false);
		return true;
	}

	bool DebugServer::consumeDevCommands(std::vector<DevCommand>& out) {
		if (!devCommandsPending.load()) {
			return false;
		}
		std::lock_guard<std::mutex> lock(devCommandsMutex);
		out.insert(out.end(), std::make_move_iterator(devCommands.begin()), std::make_move_iterator(devCommands.end()));
		devCommands.clear();
		devCommandsPending.store(false);
		return true;
	}

	void DebugServer::setCurrentSceneName(const std::string& name) {
		std::lock_guard<std::mutex> lock(sceneNameMutex);
		currentSceneName = name;
	}

	std::string DebugServer::getCurrentSceneName() const {
		std::lock_guard<std::mutex> lock(sceneNameMutex);
		return currentSceneName;
	}

	void DebugServer::waitForShutdownComplete() {
		std::unique_lock<std::mutex> lock(shutdownMutex);
		// Add timeout to prevent indefinite blocking if main loop exits abnormally
		if (!shutdownCV.wait_for(lock, std::chrono::seconds(30), [this] { return shutdownComplete; })) {
			LOG_WARNING(Foundation, "Shutdown did not complete within 30 seconds; continuing anyway.");
		}
	}

	void DebugServer::signalShutdownComplete() {
		{
			std::lock_guard<std::mutex> lock(shutdownMutex);
			shutdownComplete = true;
		}
		shutdownCV.notify_all();
	}

	bool DebugServer::requestScreenshot(std::vector<unsigned char>& pngData, int timeoutMs) { // NOLINT(readability-identifier-naming)
		LOG_INFO(Foundation, "Screenshot requested via HTTP, waiting for capture...");

		// Clear any previous ready state
		screenshotReady.store(false);

		// Request screenshot
		screenshotRequested.store(true);

		// Wait for screenshot to be ready (with timeout)
		auto startTime = std::chrono::steady_clock::now();
		while (!screenshotReady.load()) {
			auto elapsed = std::chrono::steady_clock::now() - startTime;
			if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeoutMs) {
				// Timeout - cancel request
				LOG_ERROR(Foundation, "Screenshot capture timeout after %dms", timeoutMs);
				screenshotRequested.store(false);
				return false;
			}

			// Sleep briefly to avoid busy-waiting
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		// Copy screenshot data
		{
			std::lock_guard<std::mutex> lock(screenshotMutex);
			pngData = screenshotData;
		}

		LOG_INFO(Foundation, "Screenshot data copied to HTTP response (%zu bytes)", pngData.size());

		// Clear ready flag
		screenshotReady.store(false);

		return true;
	}

	void DebugServer::serverThreadFunc(int port) { // NOLINT(readability-convert-member-functions-to-static)
		server = std::make_unique<httplib::Server>();

		// Disable SO_REUSEADDR to prevent multiple instances on same port
		server->set_socket_options([](socket_t sock) {
			int reuse = 0;
			setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
		});

		// --- REST Endpoints ---

		// Health check endpoint
		server->Get("/api/health", [this](const httplib::Request&, httplib::Response& res) {
			PerformanceMetrics metrics = getMetricsSnapshot();

			std::ostringstream json;
			json << "{";
			json << "\"status\":\"ok\",";
			json << "\"uptime\":" << metrics.timestamp;
			json << "}";

			res.set_content(json.str(), "application/json");
			res.set_header("Access-Control-Allow-Origin", "*");
		});

		// Current metrics snapshot
		server->Get("/api/metrics", [this](const httplib::Request&, httplib::Response& res) {
			PerformanceMetrics metrics = getMetricsSnapshot();
			res.set_content(metrics.toJSON(), "application/json");
			res.set_header("Access-Control-Allow-Origin", "*");
		});

		// Screenshot endpoint
		server->Get("/api/ui/screenshot", [this](const httplib::Request&, httplib::Response& res) {
			std::vector<unsigned char> pngData;

			// Request screenshot and wait for it (10 second timeout for large screenshots)
			if (requestScreenshot(pngData, 10000)) {
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

		// Input injection endpoint - queues synthetic input dispatched by the main
		// loop through the same paths as real mouse/keyboard events. Coordinates are
		// logical UI pixels. Accepts one or more 'ev' params, each a CSV:
		//   click,x,y[,left|right|middle]   (expands to move+down+up)
		//   move,x,y
		//   down,x,y[,button]   up,x,y[,button]
		//   scroll,x,y,delta
		//   keydown,<key>   keyup,<key>   (key name, e.g. R or Escape; no coords)
		// Send key events in SEPARATE requests: a keydown registers on the next frame
		// and fires the press edge (isKeyPressed) once; a later keyup releases. A
		// keydown and keyup batched in ONE request land in the same frame and collapse
		// to a release, missing the press edge. Tap example (two requests):
		//   GET /api/input?ev=keydown,R    then    GET /api/input?ev=keyup,R
		server->Get("/api/input", [this](const httplib::Request& req, httplib::Response& res) {
			res.set_header("Access-Control-Allow-Origin", "*");

			size_t count = req.get_param_value_count("ev");
			if (count == 0) {
				res.status = 400;
				res.set_content("{\"error\":\"Missing required parameter 'ev'\"}", "application/json");
				return;
			}

			std::vector<InputCommand> parsed;
			parsed.reserve(count);
			for (size_t i = 0; i < count; ++i) {
				std::string ev = req.get_param_value("ev", i);
				InputCommand cmd;
				std::string error;
				if (!parseInputEvent(ev, cmd, error)) {
					res.status = 400;
					std::ostringstream json;
					json << "{\"error\":\"" << escapeJsonString(error)
						 << "\",\"ev\":\"" << escapeJsonString(ev) << "\"}";
					res.set_content(json.str(), "application/json");
					return;
				}
				parsed.push_back(cmd);
			}

			{
				std::lock_guard<std::mutex> lock(inputCommandsMutex);
				inputCommands.insert(inputCommands.end(), parsed.begin(), parsed.end());
				inputCommandsPending.store(true);
			}

			std::ostringstream json;
			json << "{\"status\":\"ok\",\"queued\":" << parsed.size() << "}";
			res.set_content(json.str(), "application/json");
		});

		// Dev/test command endpoint - queues a generic DevCommand the app drains and
		// interprets. DebugServer stays domain-agnostic: it parses the verb (path
		// tail) plus every query param into a bag and queues it; the app (GameScene)
		// knows what "freebuild"/"givewood"/"foundation" mean. Dev-only, gated by the
		// debug server (which only runs in dev builds), mirrors /api/input's queue.
		//   /api/dev/freebuild?on=1
		//   /api/dev/givewood?n=100[&where=site|loose]
		//   /api/dev/foundation?pts=x0,y0;x1,y1;...&material=Wood&built=1
		server->Get(R"(/api/dev/[A-Za-z0-9_-]+)", [this](const httplib::Request& req, httplib::Response& res) {
			res.set_header("Access-Control-Allow-Origin", "*");

			DevCommand cmd;
			cmd.verb = parseDevVerb(req.path);
			if (cmd.verb.empty()) {
				res.status = 400;
				res.set_content("{\"error\":\"Missing dev command verb (expected /api/dev/<verb>)\"}", "application/json");
				return;
			}

			for (const auto& [key, value] : req.params) {
				cmd.params.emplace_back(key, value);
			}

			std::string verb = cmd.verb;
			{
				std::lock_guard<std::mutex> lock(devCommandsMutex);
				devCommands.push_back(std::move(cmd));
				devCommandsPending.store(true);
			}

			std::ostringstream json;
			json << "{\"status\":\"ok\",\"verb\":\"" << escapeJsonString(verb) << "\",\"queued\":1}";
			res.set_content(json.str(), "application/json");
		});

		// Control endpoint - allows control of sandbox via HTTP GET with query params
		// Examples: /api/control?action=exit
		//           /api/control?action=scene&scene=arena
		//           /api/control?action=pause
		server->Get("/api/control", [this](const httplib::Request& req, httplib::Response& res) {
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
				controlAction.store(ControlAction::Exit);

				// Block until main loop signals shutdown is complete
				waitForShutdownComplete();

				// Set response - this will be sent when handler returns
				res.set_content("{\"status\":\"ok\",\"action\":\"exit\",\"shutdown\":\"complete\"}", "application/json");
				res.set_header("Connection", "close"); // Tell client not to keep-alive

				// Signal that the handler is done and response is set
				// Main thread will call stop() after this
				{
					std::lock_guard<std::mutex> lock(shutdownMutex);
					handlerDone = true;
				}
				shutdownCV.notify_all();

				// Note: We DON'T call stop() here - the main thread does it
				// after it sees handlerDone. This avoids the assertion failure
				// from calling stop() on a handler thread.
			} else if (action == "scene") {
				// Scene change requires 'scene' parameter
				if (!req.has_param("scene")) {
					res.status = 400;
					res.set_content("{\"error\":\"Scene change requires 'scene' parameter\"}", "application/json");
					return;
				}

				std::string sceneName = req.get_param_value("scene");
				{
					std::lock_guard<std::mutex> lock(sceneNameMutex);
					targetSceneName = sceneName;
					controlAction.store(ControlAction::SceneChange);
				}

				std::ostringstream json;
				json << "{\"status\":\"ok\",\"action\":\"scene\",\"scene\":\"" << sceneName << "\"}";
				res.set_content(json.str(), "application/json");
			} else if (action == "pause") {
				controlAction.store(ControlAction::Pause);
				res.set_content("{\"status\":\"ok\",\"action\":\"pause\"}", "application/json");
			} else if (action == "resume") {
				controlAction.store(ControlAction::Resume);
				res.set_content("{\"status\":\"ok\",\"action\":\"resume\"}", "application/json");
			} else if (action == "reload") {
				controlAction.store(ControlAction::ReloadScene);
				res.set_content("{\"status\":\"ok\",\"action\":\"reload\"}", "application/json");
			} else if (action == "camera") {
				// Remote camera control: any of x, y (world position), zoom (factor),
				// panx, pany (-1..1 held-key style direction) may be supplied.
				// Parses with std::from_chars-style validation: malformed values are a 400,
				// never an exception (this runs on the HTTP handler thread).
				auto parseFloat = [&req](const char* name, float& out) -> bool {
					if (!req.has_param(name)) {
						return false;
					}
					const std::string& value = req.get_param_value(name);
					char*			   end = nullptr;
					float			   parsed = std::strtof(value.c_str(), &end);
					if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed)) {
						return false;
					}
					out = parsed;
					return true;
				};

				CameraCommand cmd;
				bool		  malformed = false;
				if (req.has_param("x") || req.has_param("y")) {
					cmd.hasPosition = parseFloat("x", cmd.x) && parseFloat("y", cmd.y);
					malformed |= !cmd.hasPosition;
				}
				if (req.has_param("zoom")) {
					cmd.hasZoom = parseFloat("zoom", cmd.zoom);
					malformed |= !cmd.hasZoom;
				}
				if (req.has_param("panx") || req.has_param("pany")) {
					cmd.hasPan = true;
					if (req.has_param("panx") && !parseFloat("panx", cmd.panX)) {
						malformed = true;
					}
					if (req.has_param("pany") && !parseFloat("pany", cmd.panY)) {
						malformed = true;
					}
					cmd.panX = std::clamp(cmd.panX, -1.0F, 1.0F);
					cmd.panY = std::clamp(cmd.panY, -1.0F, 1.0F);
				}
				if (malformed) {
					res.status = 400;
					res.set_content("{\"error\":\"Camera parameters must be finite numbers (x+y together, zoom, panx, pany)\"}", "application/json");
					return;
				}
				if (!cmd.hasPosition && !cmd.hasZoom && !cmd.hasPan) {
					res.status = 400;
					res.set_content("{\"error\":\"Camera action requires x+y, zoom, panx, or pany parameters\"}", "application/json");
					return;
				}
				{
					std::lock_guard<std::mutex> lock(cameraCommandMutex);
					cameraCommand = cmd;
					cameraCommandPending.store(true);
				}
				res.set_content("{\"status\":\"ok\",\"action\":\"camera\"}", "application/json");
			} else if (action == "vsync") {
				std::string value = req.has_param("value") ? req.get_param_value("value") : "";
				if (value != "0" && value != "1") {
					res.status = 400;
					res.set_content("{\"error\":\"Vsync action requires 'value' parameter (0 or 1)\"}", "application/json");
					return;
				}
				targetVsync.store(value == "1" ? 1 : 0);
				controlAction.store(ControlAction::SetVsync);
				res.set_content("{\"status\":\"ok\",\"action\":\"vsync\"}", "application/json");
			} else {
				res.status = 400;
				std::ostringstream json;
				json << "{\"error\":\"Invalid action '" << action << "'. Valid actions: exit, scene, pause, resume, reload, camera, vsync\"}";
				res.set_content(json.str(), "application/json");
			}
		});

		// --- SSE Streaming Endpoint ---

		// Real-time metrics stream (10 Hz)
		server->Get("/stream/metrics", [this](const httplib::Request&, httplib::Response& res) {
			res.set_header("Content-Type", "text/event-stream");
			res.set_header("Cache-Control", "no-cache");
			res.set_header("Connection", "keep-alive");
			res.set_header("Access-Control-Allow-Origin", "*");

			res.set_chunked_content_provider("text/event-stream", [this](size_t /*offset*/, httplib::DataSink& sink) {
				const int  updateRateHz = 10;
				const auto updateInterval = std::chrono::milliseconds(1000 / updateRateHz);
				auto	   lastUpdate = std::chrono::steady_clock::now();

				while (running.load() && sink.is_writable()) {
					auto now = std::chrono::steady_clock::now();
					auto elapsed = now - lastUpdate;

					// Send update if enough time has passed
					if (elapsed >= updateInterval) {
						PerformanceMetrics metrics = getMetricsSnapshot();
						std::string		   sceneName = getCurrentSceneName();

						// Get base metrics JSON and inject sceneName before closing brace
						std::string metricsJson = metrics.toJSON();
						metricsJson.pop_back(); // Remove trailing '}'
						metricsJson += ",\"sceneName\":\"" + escapeJsonString(sceneName) + "\"}";

						std::ostringstream event;
						event << "event: metric\n";
						event << "data: " << metricsJson << "\n\n";

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
		server->Get("/stream/logs", [this](const httplib::Request&, httplib::Response& res) {
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

				while (running.load() && sink.is_writable()) {
					auto now = std::chrono::steady_clock::now();
					auto elapsed = now - lastUpdate;

					// Send updates at throttled rate
					if (elapsed >= updateInterval) {
						// Read all available log entries from ring buffer
						LogEntry entry;
						while (logBuffer.read(entry)) {
							// Only send logs we haven't sent yet
							// Use >= to handle multiple logs with same timestamp
							if (entry.timestamp >= lastSentTimestamp) {
								std::ostringstream event;
								event << "event: log\n";
								event << "data: " << entry.toJSON() << "\n\n";

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
		server->Get("/", [](const httplib::Request&, httplib::Response& res) {
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
		if (!server->listen("127.0.0.1", port)) {
			std::cerr << "\n";
			std::cerr << "ERROR: Port " << port << " is already in use.\n";
			std::cerr << "An instance of the sandbox is already running.\n";
			std::cerr << "Use the following command to kill it:\n";
			std::cerr << "  curl http://127.0.0.1:" << port << "/api/control?action=exit\n";
			std::cerr << "\n";
			running.store(false);
			std::exit(1);
		}
	}

} // namespace Foundation
