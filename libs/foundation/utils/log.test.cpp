#include <gtest/gtest.h>
#include "log.h"

using namespace foundation;

// ============================================================================
// Logger Initialization Tests
// ============================================================================

TEST(LoggerTests, InitializeAndShutdown) {
	// Logger should start uninitialized
	// After Initialize, it should set default levels
	Logger::Initialize();

	// Verify that we can get levels for all categories
	LogLevel level = Logger::GetLevel(LogCategory::Renderer);
	EXPECT_TRUE(level == LogLevel::Debug || level == LogLevel::Info ||
	            level == LogLevel::Warning || level == LogLevel::Error);

	Logger::Shutdown();

	// Should be able to re-initialize
	Logger::Initialize();
	Logger::Shutdown();
}

TEST(LoggerTests, MultipleInitializeCalls) {
	// Multiple Initialize calls should be safe (idempotent)
	Logger::Initialize();
	Logger::Initialize();
	Logger::Initialize();

	LogLevel level = Logger::GetLevel(LogCategory::Game);
	EXPECT_TRUE(level == LogLevel::Debug || level == LogLevel::Info ||
	            level == LogLevel::Warning || level == LogLevel::Error);

	Logger::Shutdown();
}

// ============================================================================
// Log Level Management Tests
// ============================================================================

TEST(LoggerTests, SetAndGetLevel) {
	Logger::Initialize();

	// Set different levels for different categories
	Logger::SetLevel(LogCategory::Renderer, LogLevel::Debug);
	Logger::SetLevel(LogCategory::Physics, LogLevel::Info);
	Logger::SetLevel(LogCategory::Audio, LogLevel::Warning);
	Logger::SetLevel(LogCategory::Network, LogLevel::Error);

	// Verify they were set correctly
	EXPECT_EQ(Logger::GetLevel(LogCategory::Renderer), LogLevel::Debug);
	EXPECT_EQ(Logger::GetLevel(LogCategory::Physics), LogLevel::Info);
	EXPECT_EQ(Logger::GetLevel(LogCategory::Audio), LogLevel::Warning);
	EXPECT_EQ(Logger::GetLevel(LogCategory::Network), LogLevel::Error);

	Logger::Shutdown();
}

TEST(LoggerTests, SetLevelForAllCategories) {
	Logger::Initialize();

	// Set all categories to Warning level
	Logger::SetLevel(LogCategory::Renderer, LogLevel::Warning);
	Logger::SetLevel(LogCategory::Physics, LogLevel::Warning);
	Logger::SetLevel(LogCategory::Audio, LogLevel::Warning);
	Logger::SetLevel(LogCategory::Network, LogLevel::Warning);
	Logger::SetLevel(LogCategory::Game, LogLevel::Warning);
	Logger::SetLevel(LogCategory::World, LogLevel::Warning);
	Logger::SetLevel(LogCategory::UI, LogLevel::Warning);
	Logger::SetLevel(LogCategory::Engine, LogLevel::Warning);
	Logger::SetLevel(LogCategory::Foundation, LogLevel::Warning);

	// Verify all categories
	EXPECT_EQ(Logger::GetLevel(LogCategory::Renderer), LogLevel::Warning);
	EXPECT_EQ(Logger::GetLevel(LogCategory::Physics), LogLevel::Warning);
	EXPECT_EQ(Logger::GetLevel(LogCategory::Audio), LogLevel::Warning);
	EXPECT_EQ(Logger::GetLevel(LogCategory::Network), LogLevel::Warning);
	EXPECT_EQ(Logger::GetLevel(LogCategory::Game), LogLevel::Warning);
	EXPECT_EQ(Logger::GetLevel(LogCategory::World), LogLevel::Warning);
	EXPECT_EQ(Logger::GetLevel(LogCategory::UI), LogLevel::Warning);
	EXPECT_EQ(Logger::GetLevel(LogCategory::Engine), LogLevel::Warning);
	EXPECT_EQ(Logger::GetLevel(LogCategory::Foundation), LogLevel::Warning);

	Logger::Shutdown();
}

TEST(LoggerTests, DefaultLevels) {
	Logger::Initialize();

	// In development builds, default levels should be Debug or Info
	// In release builds, default levels should be Error
	// We just verify that levels are reasonable

#ifdef DEVELOPMENT_BUILD
	// Game category defaults to Debug in development
	EXPECT_EQ(Logger::GetLevel(LogCategory::Game), LogLevel::Debug);

	// Most other categories default to Info
	LogLevel rendererLevel = Logger::GetLevel(LogCategory::Renderer);
	EXPECT_TRUE(rendererLevel == LogLevel::Debug || rendererLevel == LogLevel::Info);
#else
	// Release builds: only errors
	EXPECT_EQ(Logger::GetLevel(LogCategory::Renderer), LogLevel::Error);
	EXPECT_EQ(Logger::GetLevel(LogCategory::Game), LogLevel::Error);
#endif

	Logger::Shutdown();
}

// ============================================================================
// Helper Function Tests
// ============================================================================

TEST(LoggerTests, CategoryToString) {
	EXPECT_STREQ(CategoryToString(LogCategory::Renderer), "Renderer");
	EXPECT_STREQ(CategoryToString(LogCategory::Physics), "Physics");
	EXPECT_STREQ(CategoryToString(LogCategory::Audio), "Audio");
	EXPECT_STREQ(CategoryToString(LogCategory::Network), "Network");
	EXPECT_STREQ(CategoryToString(LogCategory::Game), "Game");
	EXPECT_STREQ(CategoryToString(LogCategory::World), "World");
	EXPECT_STREQ(CategoryToString(LogCategory::UI), "UI");
	EXPECT_STREQ(CategoryToString(LogCategory::Engine), "Engine");
	EXPECT_STREQ(CategoryToString(LogCategory::Foundation), "Foundation");
}

TEST(LoggerTests, LevelToString) {
	EXPECT_STREQ(LevelToString(LogLevel::Debug), "DEBUG");
	EXPECT_STREQ(LevelToString(LogLevel::Info), "INFO");
	EXPECT_STREQ(LevelToString(LogLevel::Warning), "WARN");
	EXPECT_STREQ(LevelToString(LogLevel::Error), "ERROR");
}

// ============================================================================
// Log Macro Tests
// ============================================================================

TEST(LoggerTests, LogMacrosCompile) {
	Logger::Initialize();
	Logger::SetLevel(LogCategory::Foundation, LogLevel::Debug);

	// These should compile and not crash
	// We can't easily test the output, but we can verify they execute
	LOG_DEBUG(Foundation, "Debug message: %d", 42);
	LOG_INFO(Foundation, "Info message: %s", "test");
	LOG_WARNING(Foundation, "Warning message: %f", 3.14);
	LOG_ERROR(Foundation, "Error message: %d %s", 123, "error");

	Logger::Shutdown();
}

TEST(LoggerTests, LogMacrosWithDifferentCategories) {
	Logger::Initialize();

	// Set all categories to Debug to ensure messages would log
	Logger::SetLevel(LogCategory::Renderer, LogLevel::Debug);
	Logger::SetLevel(LogCategory::Physics, LogLevel::Debug);
	Logger::SetLevel(LogCategory::Audio, LogLevel::Debug);
	Logger::SetLevel(LogCategory::Network, LogLevel::Debug);
	Logger::SetLevel(LogCategory::Game, LogLevel::Debug);
	Logger::SetLevel(LogCategory::World, LogLevel::Debug);
	Logger::SetLevel(LogCategory::UI, LogLevel::Debug);
	Logger::SetLevel(LogCategory::Engine, LogLevel::Debug);
	Logger::SetLevel(LogCategory::Foundation, LogLevel::Debug);

	// Test each category (these should all compile and execute)
	LOG_INFO(Renderer, "Renderer message");
	LOG_INFO(Physics, "Physics message");
	LOG_INFO(Audio, "Audio message");
	LOG_INFO(Network, "Network message");
	LOG_INFO(Game, "Game message");
	LOG_INFO(World, "World message");
	LOG_INFO(UI, "UI message");
	LOG_INFO(Engine, "Engine message");
	LOG_INFO(Foundation, "Foundation message");

	Logger::Shutdown();
}

// ============================================================================
// State Management Tests
// ============================================================================

TEST(LoggerTests, LevelPersistsAcrossMultipleGets) {
	Logger::Initialize();

	Logger::SetLevel(LogCategory::Game, LogLevel::Debug);

	// Multiple gets should return the same value
	EXPECT_EQ(Logger::GetLevel(LogCategory::Game), LogLevel::Debug);
	EXPECT_EQ(Logger::GetLevel(LogCategory::Game), LogLevel::Debug);
	EXPECT_EQ(Logger::GetLevel(LogCategory::Game), LogLevel::Debug);

	// Change level
	Logger::SetLevel(LogCategory::Game, LogLevel::Error);

	// New level should persist
	EXPECT_EQ(Logger::GetLevel(LogCategory::Game), LogLevel::Error);
	EXPECT_EQ(Logger::GetLevel(LogCategory::Game), LogLevel::Error);

	Logger::Shutdown();
}

TEST(LoggerTests, IndependentCategoryLevels) {
	Logger::Initialize();

	// Set different levels
	Logger::SetLevel(LogCategory::Renderer, LogLevel::Debug);
	Logger::SetLevel(LogCategory::Physics, LogLevel::Info);
	Logger::SetLevel(LogCategory::Game, LogLevel::Warning);

	// Changing one should not affect others
	Logger::SetLevel(LogCategory::Renderer, LogLevel::Error);

	EXPECT_EQ(Logger::GetLevel(LogCategory::Renderer), LogLevel::Error);
	EXPECT_EQ(Logger::GetLevel(LogCategory::Physics), LogLevel::Info);
	EXPECT_EQ(Logger::GetLevel(LogCategory::Game), LogLevel::Warning);

	Logger::Shutdown();
}

// ============================================================================
// DebugServer Integration Tests (Compile-time checks)
// ============================================================================

TEST(LoggerTests, SetDebugServerDoesNotCrash) {
	Logger::Initialize();

	// Setting null debug server should not crash
	Logger::SetDebugServer(nullptr);

	// Logging after setting null debug server should not crash
	LOG_INFO(Foundation, "Test message with null debug server");

	Logger::Shutdown();
}
