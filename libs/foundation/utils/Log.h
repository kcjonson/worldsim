#pragma once

#include <atomic>
#include <cstdarg>
#include <cstdint>

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

	// One formatted log call. Handed to the sink BEFORE the console level filter
	// (the sink sees every message and filters itself). message is null-terminated,
	// at most 255 chars, and valid only for the duration of the call.
	struct LogRecord {
		LogCategory category;
		LogLevel	level;
		const char* message;
		const char* file; // __FILE__ of the call site (static storage)
		int			line;
	};

	class Logger {
	  public:
		static void initialize();
		static void shutdown();

		static void		setLevel(LogCategory category, LogLevel level);
		static LogLevel getLevel(LogCategory category);

		static void log(LogCategory category, LogLevel level, const char* file, int line, const char* format, ...);

		// Called for every formatted message BEFORE the console level filter, from whichever
		// thread logged. Development builds only.
		using LogSinkFn = void (*)(void* context, const LogRecord& record);

		// Install (fn non-null) before worker threads exist. Clear (fn null) is safe while they
		// still log: a thread that already loaded the function pointer calls it with the
		// context, which must outlive the clear (AppLauncher clears before it stops the server).
		static void setSink(LogSinkFn fn, void* context);

	  private:
		static LogLevel s_levels[static_cast<int>(LogCategory::Count)]; // NOLINT(readability-identifier-naming)
		static bool		s_initialized;									// NOLINT(readability-identifier-naming)

#ifdef DEVELOPMENT_BUILD
		static std::atomic<LogSinkFn> s_sinkFn;		 // NOLINT(readability-identifier-naming)
		static std::atomic<void*>	  s_sinkContext; // NOLINT(readability-identifier-naming)
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
