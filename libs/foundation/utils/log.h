#pragma once

#include <cstdarg>
#include <cstdint>

// Forward declare DebugServer to avoid circular dependency
namespace Foundation { // NOLINT(readability-identifier-naming)
	class DebugServer;
}

namespace foundation {

	enum class LogLevel : std::uint8_t { // NOLINT(performance-enum-size)
		Debug,
		Info,
		Warning,
		Error
	};

	enum class LogCategory : std::uint8_t { // NOLINT(performance-enum-size)
		Renderer,
		Physics,
		Audio,
		Network,
		Game,
		World,
		UI,
		Engine,
		Foundation,
		Count
	};

	class Logger {
	  public:
		static void initialize();
		static void shutdown();

		static void		setLevel(LogCategory category, LogLevel level);
		static LogLevel getLevel(LogCategory category);

		static void log(LogCategory category, LogLevel level, const char* file, int line, const char* format, ...);

		// Set debug server for HTTP streaming (Development builds only)
		// IMPORTANT: DebugServer must outlive Logger, or call setDebugServer(nullptr) before destroying it
		static void setDebugServer(Foundation::DebugServer* debugServer);

	  private:
		static LogLevel s_levels[static_cast<int>(LogCategory::Count)]; // NOLINT(readability-identifier-naming)
		static bool		s_initialized;									// NOLINT(readability-identifier-naming)

#ifdef DEVELOPMENT_BUILD
		static Foundation::DebugServer* s_debugServer; // NOLINT(readability-identifier-naming)
#endif
	};

	// Helper functions to convert enums to strings
	const char* categoryToString(LogCategory cat);
	const char* levelToString(LogLevel level);

} // namespace foundation

// Convenience macros that automatically capture file and line
// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define LOG_DEBUG(category, format, ...)                                                                                                   \
	foundation::Logger::log(foundation::LogCategory::category, foundation::LogLevel::Debug, __FILE__, __LINE__, format, ##__VA_ARGS__)

#define LOG_INFO(category, format, ...)                                                                                                    \
	foundation::Logger::log(foundation::LogCategory::category, foundation::LogLevel::Info, __FILE__, __LINE__, format, ##__VA_ARGS__)

#define LOG_WARNING(category, format, ...)                                                                                                 \
	foundation::Logger::log(foundation::LogCategory::category, foundation::LogLevel::Warning, __FILE__, __LINE__, format, ##__VA_ARGS__)

#define LOG_ERROR(category, format, ...)                                                                                                   \
	foundation::Logger::log(foundation::LogCategory::category, foundation::LogLevel::Error, __FILE__, __LINE__, format, ##__VA_ARGS__)

// Compile out Debug, Info, and Warning logs in Release builds
#ifndef DEVELOPMENT_BUILD
#undef LOG_DEBUG
#define LOG_DEBUG(category, format, ...) ((void)0)

#undef LOG_INFO
#define LOG_INFO(category, format, ...) ((void)0)

#undef LOG_WARNING
#define LOG_WARNING(category, format, ...) ((void)0)

// Only LOG_ERROR remains in Release builds
#endif
// NOLINTEND(cppcoreguidelines-macro-usage)
