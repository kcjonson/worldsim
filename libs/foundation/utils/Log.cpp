#include "Log.h"
#include "debug/DebugServer.h"

#include <cstdio>
#include <ctime>

namespace foundation {

	// Static member initialization
	LogLevel Logger::s_levels[static_cast<int>(LogCategory::Count)];
	bool	 Logger::s_initialized = false;

#ifdef DEVELOPMENT_BUILD
	Foundation::DebugServer* Logger::s_debugServer = nullptr;
#endif

	// Helper: Convert foundation::LogLevel to Foundation::LogLevel
	static Foundation::LogLevel convertLogLevel(LogLevel level) {
		switch (level) {
			case LogLevel::Debug:
				return Foundation::LogLevel::Debug;
			case LogLevel::Info:
				return Foundation::LogLevel::Info;
			case LogLevel::Warning:
				return Foundation::LogLevel::Warning;
			case LogLevel::Error:
				return Foundation::LogLevel::Error;
			default:
				return Foundation::LogLevel::Info;
		}
	}

	// Helper: Convert foundation::LogCategory to Foundation::LogCategory
	static Foundation::LogCategory convertLogCategory(LogCategory category) {
		switch (category) {
			case LogCategory::Renderer:
				return Foundation::LogCategory::Renderer;
			case LogCategory::Physics:
				return Foundation::LogCategory::Physics;
			case LogCategory::Audio:
				return Foundation::LogCategory::Audio;
			case LogCategory::Network:
				return Foundation::LogCategory::Network;
			case LogCategory::Game:
				return Foundation::LogCategory::Game;
			case LogCategory::World:
				return Foundation::LogCategory::World;
			case LogCategory::UI:
				return Foundation::LogCategory::UI;
			case LogCategory::Engine:
				return Foundation::LogCategory::Engine;
			case LogCategory::Foundation: // NOLINT(bugprone-branch-clone)
			case LogCategory::Count:	  // NOLINT(clang-diagnostic-switch)
				return Foundation::LogCategory::Foundation;
			default: // NOLINT(bugprone-branch-clone)
				return Foundation::LogCategory::Foundation;
		}
	}

	void Logger::initialize() {
		if (s_initialized) {
			return;
		}

		// Set default log levels for each category
		// Development: Debug for most categories, Info for less verbose ones
		// These can be overridden by config or runtime calls to setLevel()
#ifdef DEVELOPMENT_BUILD
		s_levels[static_cast<int>(LogCategory::Renderer)] = LogLevel::Info;
		s_levels[static_cast<int>(LogCategory::Physics)] = LogLevel::Info;
		s_levels[static_cast<int>(LogCategory::Audio)] = LogLevel::Info;
		s_levels[static_cast<int>(LogCategory::Network)] = LogLevel::Info;
		s_levels[static_cast<int>(LogCategory::Game)] = LogLevel::Debug;
		s_levels[static_cast<int>(LogCategory::World)] = LogLevel::Info;
		s_levels[static_cast<int>(LogCategory::UI)] = LogLevel::Info;
		s_levels[static_cast<int>(LogCategory::Engine)] = LogLevel::Info;
		s_levels[static_cast<int>(LogCategory::Foundation)] = LogLevel::Info;
#else
		// Release builds: only errors
		for (int i = 0; i < static_cast<int>(LogCategory::Count); i++) {
			s_levels[i] = LogLevel::Error;
		}
#endif

		s_initialized = true;
	}

	void Logger::shutdown() {
#ifdef DEVELOPMENT_BUILD
		s_debugServer = nullptr;
#endif
		s_initialized = false;
	}

	void Logger::setDebugServer(Foundation::DebugServer* debugServer) {
#ifdef DEVELOPMENT_BUILD
		s_debugServer = debugServer;
#endif
	}

	void Logger::setLevel(LogCategory category, LogLevel level) {
		s_levels[static_cast<int>(category)] = level; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
	}

	LogLevel Logger::getLevel(LogCategory category) {
		return s_levels[static_cast<int>(category)]; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
	}

	void Logger::log(LogCategory category, LogLevel level, const char* file, int line, const char* format, ...) {
		// Format message first (needed for both console and debug server)
		char	message[256];
		va_list args;
		va_start(args, format);
		vsnprintf(message, sizeof(message), format, args);
		va_end(args);

#ifdef DEVELOPMENT_BUILD
		// ALWAYS send to debug server (regardless of console filter)
		// Lock-free write, never blocks (~10-20 nanoseconds)
		// Developer client has its own filtering UI
		if (s_debugServer != nullptr) { // NOLINT(readability-implicit-bool-conversion)
			s_debugServer->updateLog(convertLogLevel(level), convertLogCategory(category), message, file, line);
		}
#endif

		// Filter by level for console output
		if (level < getLevel(category)) {
			return; // Too verbose for console, skip console output
		}

		// Get timestamp
		time_t now = time(nullptr);
		tm*	   timeinfo = localtime(&now);
		char   timestamp[32];
		strftime(timestamp, sizeof(timestamp), "%H:%M:%S", timeinfo);

		// ANSI color codes for different levels
		const char* colorCode = "";
		const char* resetCode = "\033[0m";

#ifdef DEVELOPMENT_BUILD
		switch (level) {
			case LogLevel::Debug:
				colorCode = "\033[90m";
				break; // Gray
			case LogLevel::Info:
				colorCode = "\033[37m";
				break; // White
			case LogLevel::Warning:
				colorCode = "\033[33m";
				break; // Yellow
			case LogLevel::Error:
				colorCode = "\033[31m";
				break; // Red
		}
#else
		resetCode = ""; // No colors in release builds
#endif

		// Print to console: [TIME][CATEGORY][LEVEL] message
		printf("%s[%s][%s][%s]%s %s", colorCode, timestamp, categoryToString(category), levelToString(level), resetCode, message);

		// Add file:line for errors and warnings
		if (level >= LogLevel::Warning) {
			printf(" (%s:%d)", file, line);
		}

		printf("\n");
		fflush(stdout);
	}

	const char* categoryToString(LogCategory cat) {
		switch (cat) {
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

	const char* levelToString(LogLevel level) {
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
				return "?????";
		}
	}

} // namespace foundation
