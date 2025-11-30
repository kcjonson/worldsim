#include "log.h"
#include <gtest/gtest.h>

using namespace foundation;

// ============================================================================
// Logger Initialization Tests
// ============================================================================

TEST(LoggerTests, InitializeAndShutdown) {
	// Logger should start uninitialized
	// After Initialize, it should set default levels
	Logger::initialize();

	// Verify that we can get levels for all categories
	LogLevel level = Logger::getLevel(LogCategory::Renderer);
	EXPECT_TRUE(level == LogLevel::Debug || level == LogLevel::Info || level == LogLevel::Warning || level == LogLevel::Error);

	Logger::shutdown();

	// Should be able to re-initialize
	Logger::initialize();
	Logger::shutdown();
}

TEST(LoggerTests, MultipleInitializeCalls) {
	// Multiple Initialize calls should be safe (idempotent)
	Logger::initialize();
	Logger::initialize();
	Logger::initialize();

	LogLevel level = Logger::getLevel(LogCategory::Game);
	EXPECT_TRUE(level == LogLevel::Debug || level == LogLevel::Info || level == LogLevel::Warning || level == LogLevel::Error);

	Logger::shutdown();
}

// ============================================================================
// Log Level Management Tests
// ============================================================================

TEST(LoggerTests, SetAndGetLevel) {
	Logger::initialize();

	// Set different levels for different categories
	Logger::setLevel(LogCategory::Renderer, LogLevel::Debug);
	Logger::setLevel(LogCategory::Physics, LogLevel::Info);
	Logger::setLevel(LogCategory::Audio, LogLevel::Warning);
	Logger::setLevel(LogCategory::Network, LogLevel::Error);

	// Verify they were set correctly
	EXPECT_EQ(Logger::getLevel(LogCategory::Renderer), LogLevel::Debug);
	EXPECT_EQ(Logger::getLevel(LogCategory::Physics), LogLevel::Info);
	EXPECT_EQ(Logger::getLevel(LogCategory::Audio), LogLevel::Warning);
	EXPECT_EQ(Logger::getLevel(LogCategory::Network), LogLevel::Error);

	Logger::shutdown();
}

TEST(LoggerTests, SetLevelForAllCategories) {
	Logger::initialize();

	// Set all categories to Warning level
	Logger::setLevel(LogCategory::Renderer, LogLevel::Warning);
	Logger::setLevel(LogCategory::Physics, LogLevel::Warning);
	Logger::setLevel(LogCategory::Audio, LogLevel::Warning);
	Logger::setLevel(LogCategory::Network, LogLevel::Warning);
	Logger::setLevel(LogCategory::Game, LogLevel::Warning);
	Logger::setLevel(LogCategory::World, LogLevel::Warning);
	Logger::setLevel(LogCategory::UI, LogLevel::Warning);
	Logger::setLevel(LogCategory::Engine, LogLevel::Warning);
	Logger::setLevel(LogCategory::Foundation, LogLevel::Warning);

	// Verify all categories
	EXPECT_EQ(Logger::getLevel(LogCategory::Renderer), LogLevel::Warning);
	EXPECT_EQ(Logger::getLevel(LogCategory::Physics), LogLevel::Warning);
	EXPECT_EQ(Logger::getLevel(LogCategory::Audio), LogLevel::Warning);
	EXPECT_EQ(Logger::getLevel(LogCategory::Network), LogLevel::Warning);
	EXPECT_EQ(Logger::getLevel(LogCategory::Game), LogLevel::Warning);
	EXPECT_EQ(Logger::getLevel(LogCategory::World), LogLevel::Warning);
	EXPECT_EQ(Logger::getLevel(LogCategory::UI), LogLevel::Warning);
	EXPECT_EQ(Logger::getLevel(LogCategory::Engine), LogLevel::Warning);
	EXPECT_EQ(Logger::getLevel(LogCategory::Foundation), LogLevel::Warning);

	Logger::shutdown();
}

TEST(LoggerTests, DefaultLevels) {
	Logger::initialize();

	// In development builds, default levels should be Debug or Info
	// In release builds, default levels should be Error
	// We just verify that levels are reasonable

#ifdef DEVELOPMENT_BUILD
	// Game category defaults to Debug in development
	EXPECT_EQ(Logger::getLevel(LogCategory::Game), LogLevel::Debug);

	// Most other categories default to Info
	LogLevel rendererLevel = Logger::getLevel(LogCategory::Renderer);
	EXPECT_TRUE(rendererLevel == LogLevel::Debug || rendererLevel == LogLevel::Info);
#else
	// Release builds: only errors
	EXPECT_EQ(Logger::getLevel(LogCategory::Renderer), LogLevel::Error);
	EXPECT_EQ(Logger::getLevel(LogCategory::Game), LogLevel::Error);
#endif

	Logger::shutdown();
}

// ============================================================================
// Helper Function Tests
// ============================================================================

TEST(LoggerTests, categoryToString) {
	EXPECT_STREQ(categoryToString(LogCategory::Renderer), "Renderer");
	EXPECT_STREQ(categoryToString(LogCategory::Physics), "Physics");
	EXPECT_STREQ(categoryToString(LogCategory::Audio), "Audio");
	EXPECT_STREQ(categoryToString(LogCategory::Network), "Network");
	EXPECT_STREQ(categoryToString(LogCategory::Game), "Game");
	EXPECT_STREQ(categoryToString(LogCategory::World), "World");
	EXPECT_STREQ(categoryToString(LogCategory::UI), "UI");
	EXPECT_STREQ(categoryToString(LogCategory::Engine), "Engine");
	EXPECT_STREQ(categoryToString(LogCategory::Foundation), "Foundation");
}

TEST(LoggerTests, levelToString) {
	EXPECT_STREQ(levelToString(LogLevel::Debug), "DEBUG");
	EXPECT_STREQ(levelToString(LogLevel::Info), "INFO");
	EXPECT_STREQ(levelToString(LogLevel::Warning), "WARN");
	EXPECT_STREQ(levelToString(LogLevel::Error), "ERROR");
}

// ============================================================================
// Log Macro Tests
// ============================================================================

TEST(LoggerTests, LogMacrosCompile) {
	Logger::initialize();
	Logger::setLevel(LogCategory::Foundation, LogLevel::Debug);

	// These should compile and not crash
	// We can't easily test the output, but we can verify they execute
	LOG_DEBUG(Foundation, "Debug message: %d", 42);
	LOG_INFO(Foundation, "Info message: %s", "test");
	LOG_WARNING(Foundation, "Warning message: %f", 3.14);
	LOG_ERROR(Foundation, "Error message: %d %s", 123, "error");

	Logger::shutdown();
}

TEST(LoggerTests, LogMacrosWithDifferentCategories) {
	Logger::initialize();

	// Set all categories to Debug to ensure messages would log
	Logger::setLevel(LogCategory::Renderer, LogLevel::Debug);
	Logger::setLevel(LogCategory::Physics, LogLevel::Debug);
	Logger::setLevel(LogCategory::Audio, LogLevel::Debug);
	Logger::setLevel(LogCategory::Network, LogLevel::Debug);
	Logger::setLevel(LogCategory::Game, LogLevel::Debug);
	Logger::setLevel(LogCategory::World, LogLevel::Debug);
	Logger::setLevel(LogCategory::UI, LogLevel::Debug);
	Logger::setLevel(LogCategory::Engine, LogLevel::Debug);
	Logger::setLevel(LogCategory::Foundation, LogLevel::Debug);

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

	Logger::shutdown();
}

// ============================================================================
// State Management Tests
// ============================================================================

TEST(LoggerTests, LevelPersistsAcrossMultipleGets) {
	Logger::initialize();

	Logger::setLevel(LogCategory::Game, LogLevel::Debug);

	// Multiple gets should return the same value
	EXPECT_EQ(Logger::getLevel(LogCategory::Game), LogLevel::Debug);
	EXPECT_EQ(Logger::getLevel(LogCategory::Game), LogLevel::Debug);
	EXPECT_EQ(Logger::getLevel(LogCategory::Game), LogLevel::Debug);

	// Change level
	Logger::setLevel(LogCategory::Game, LogLevel::Error);

	// New level should persist
	EXPECT_EQ(Logger::getLevel(LogCategory::Game), LogLevel::Error);
	EXPECT_EQ(Logger::getLevel(LogCategory::Game), LogLevel::Error);

	Logger::shutdown();
}

TEST(LoggerTests, IndependentCategoryLevels) {
	Logger::initialize();

	// Set different levels
	Logger::setLevel(LogCategory::Renderer, LogLevel::Debug);
	Logger::setLevel(LogCategory::Physics, LogLevel::Info);
	Logger::setLevel(LogCategory::Game, LogLevel::Warning);

	// Changing one should not affect others
	Logger::setLevel(LogCategory::Renderer, LogLevel::Error);

	EXPECT_EQ(Logger::getLevel(LogCategory::Renderer), LogLevel::Error);
	EXPECT_EQ(Logger::getLevel(LogCategory::Physics), LogLevel::Info);
	EXPECT_EQ(Logger::getLevel(LogCategory::Game), LogLevel::Warning);

	Logger::shutdown();
}

// ============================================================================
// DebugServer Integration Tests (Compile-time checks)
// ============================================================================

TEST(LoggerTests, SetDebugServerDoesNotCrash) {
	Logger::initialize();

	// Setting null debug server should not crash
	Logger::setDebugServer(nullptr);

	// Logging after setting null debug server should not crash
	LOG_INFO(Foundation, "Test message with null debug server");

	Logger::shutdown();
}
