#include "string_hash.h"
#include <gtest/gtest.h>

using namespace foundation;

// ============================================================================
// FNV-1a Hash Algorithm Tests
// ============================================================================

TEST(StringHashTests, HashEmptyString) {
	StringHash hash = HashString("");

	// FNV-1a offset basis for empty string
	EXPECT_EQ(hash, 0xcbf29ce484222325ULL);
}

TEST(StringHashTests, HashSingleCharacter) {
	StringHash hashA = HashString("a");
	StringHash hashB = HashString("b");

	// Different characters should produce different hashes
	EXPECT_NE(hashA, hashB);

	// Should not be zero or the offset basis
	EXPECT_NE(hashA, 0);
	EXPECT_NE(hashB, 0);
	EXPECT_NE(hashA, 0xcbf29ce484222325ULL);
}

TEST(StringHashTests, HashDifferentStrings) {
	StringHash hash1 = HashString("hello");
	StringHash hash2 = HashString("world");
	StringHash hash3 = HashString("test");

	// All should be unique
	EXPECT_NE(hash1, hash2);
	EXPECT_NE(hash1, hash3);
	EXPECT_NE(hash2, hash3);
}

TEST(StringHashTests, HashSameString) {
	StringHash hash1 = HashString("identical");
	StringHash hash2 = HashString("identical");

	// Same string should produce same hash
	EXPECT_EQ(hash1, hash2);
}

TEST(StringHashTests, HashCaseSensitive) {
	StringHash hashLower = HashString("test");
	StringHash hashUpper = HashString("TEST");
	StringHash hashMixed = HashString("Test");

	// Case differences should produce different hashes
	EXPECT_NE(hashLower, hashUpper);
	EXPECT_NE(hashLower, hashMixed);
	EXPECT_NE(hashUpper, hashMixed);
}

TEST(StringHashTests, HashLongString) {
	const char* longString = "This is a very long string with many characters to test the hash function";
	StringHash	hash = HashString(longString);

	// Should produce a valid hash
	EXPECT_NE(hash, 0);

	// Hashing again should produce same result
	StringHash hash2 = HashString(longString);
	EXPECT_EQ(hash, hash2);
}

TEST(StringHashTests, HashSimilarStrings) {
	StringHash hash1 = HashString("test1");
	StringHash hash2 = HashString("test2");
	StringHash hash3 = HashString("test_");

	// Similar strings should produce different hashes
	EXPECT_NE(hash1, hash2);
	EXPECT_NE(hash1, hash3);
	EXPECT_NE(hash2, hash3);
}

TEST(StringHashTests, HashWithSpecialCharacters) {
	StringHash hash1 = HashString("hello_world");
	StringHash hash2 = HashString("hello-world");
	StringHash hash3 = HashString("hello world");
	StringHash hash4 = HashString("hello@world");

	// All should be unique
	EXPECT_NE(hash1, hash2);
	EXPECT_NE(hash1, hash3);
	EXPECT_NE(hash1, hash4);
	EXPECT_NE(hash2, hash3);
	EXPECT_NE(hash2, hash4);
	EXPECT_NE(hash3, hash4);
}

TEST(StringHashTests, HashWithNumbers) {
	StringHash hash1 = HashString("123");
	StringHash hash2 = HashString("456");
	StringHash hash3 = HashString("123456");

	EXPECT_NE(hash1, hash2);
	EXPECT_NE(hash1, hash3);
	EXPECT_NE(hash2, hash3);
}

// ============================================================================
// Compile-Time Hashing Tests
// ============================================================================

TEST(StringHashTests, CompileTimeHashMacro) {
	// These should be compile-time constants
	constexpr StringHash kHash1 = HASH("compile_time");
	constexpr StringHash kHash2 = HASH("compile_time");
	constexpr StringHash kHash3 = HASH("different");

	EXPECT_EQ(kHash1, kHash2);
	EXPECT_NE(kHash1, kHash3);
}

TEST(StringHashTests, CompileTimeMatchesRuntime) {
	constexpr StringHash kCompileTime = HASH("test_string");
	StringHash			 runtime = HashString("test_string");

	// Compile-time and runtime should produce identical results
	EXPECT_EQ(kCompileTime, runtime);
}

TEST(StringHashTests, ConstexprHashString) {
	// Verify HashString is actually constexpr
	constexpr StringHash kHash = HashString("constexpr_test");

	EXPECT_NE(kHash, 0);
	EXPECT_EQ(kHash, HashString("constexpr_test"));
}

// ============================================================================
// Common Hash Constants Tests
// ============================================================================

TEST(StringHashTests, CommonHashConstants) {
	// Verify common constants are defined
	EXPECT_EQ(hashes::kTransform, HASH("Transform"));
	EXPECT_EQ(hashes::kPosition, HASH("Position"));
	EXPECT_EQ(hashes::kVelocity, HASH("Velocity"));
	EXPECT_EQ(hashes::kRenderable, HASH("Renderable"));

	EXPECT_EQ(hashes::kTexture, HASH("Texture"));
	EXPECT_EQ(hashes::kShader, HASH("Shader"));
	EXPECT_EQ(hashes::kMesh, HASH("Mesh"));

	EXPECT_EQ(hashes::kWidth, HASH("width"));
	EXPECT_EQ(hashes::kHeight, HASH("height"));
	EXPECT_EQ(hashes::kFullscreen, HASH("fullscreen"));
}

TEST(StringHashTests, CommonHashConstantsUnique) {
	// Verify no collisions in common constants
	std::vector<StringHash> commonHashes = {
		hashes::kTransform,
		hashes::kPosition,
		hashes::kVelocity,
		hashes::kRenderable,
		hashes::kTexture,
		hashes::kShader,
		hashes::kMesh,
		hashes::kWidth,
		hashes::kHeight,
		hashes::kFullscreen
	};

	// Check all are unique
	for (size_t i = 0; i < commonHashes.size(); i++) {
		for (size_t j = i + 1; j < commonHashes.size(); j++) {
			EXPECT_NE(commonHashes[i], commonHashes[j])
				<< "Collision found between common hash constants at indices " << i // NOLINT(readability-implicit-bool-conversion)
				<< " and "															// NOLINT(readability-implicit-bool-conversion)
				<< j;																// NOLINT(readability-implicit-bool-conversion)
		}
	}
}

// ============================================================================
// Hash Distribution Tests (Statistical)
// ============================================================================

TEST(StringHashTests, HashDistribution) {
	// Generate many hashes and check distribution
	std::vector<StringHash> hashes;
	hashes.reserve(1000);

	for (int i = 0; i < 1000; i++) {
		std::string str = "string_" + std::to_string(i);
		hashes.push_back(HashString(str.c_str()));
	}

	// Check all are unique (no collisions in 1000 sequential strings)
	for (size_t i = 0; i < hashes.size(); i++) {
		for (size_t j = i + 1; j < hashes.size(); j++) {
			EXPECT_NE(hashes[i], hashes[j]) << "Collision between 'string_" << i
											<< "' and 'string_" // NOLINT(readability-implicit-bool-conversion)
											<< j				// NOLINT(readability-implicit-bool-conversion)
											<< "'";				// NOLINT(readability-implicit-bool-conversion)
		}
	}
}

TEST(StringHashTests, NoZeroHashes) {
	// Verify that common strings don't hash to zero
	std::vector<const char*> commonStrings = {
		"", "a", "ab", "abc", "test", "hello", "world", "Transform", "Position", "Velocity", "0", "1", "123", "null", "nullptr"
	};

	for (const char* str : commonStrings) {
		StringHash hash = HashString(str);
		// Only empty string should hash to FNV offset basis
		if (str[0] == '\0') {
			EXPECT_EQ(hash, 0xcbf29ce484222325ULL);
		}
		// Others should not be zero
		else {
			EXPECT_NE(hash, 0) << "String '" << str << "' hashed to zero";
		}
	}
}

// ============================================================================
// Debug-Only Features Tests
// ============================================================================

#ifdef DEBUG

TEST(StringHashDebugTests, HashStringDebug) {
	// HashStringDebug should produce same results as HashString
	StringHash hash1 = HashString("debug_test");
	StringHash hash2 = HashStringDebug("debug_test");

	EXPECT_EQ(hash1, hash2);
}

TEST(StringHashDebugTests, GetStringForHash) {
	// Register a hash
	const char* original = "test_lookup";
	StringHash	hash = HashStringDebug(original);

	// Look it up
	const char* retrieved = GetStringForHash(hash);
	EXPECT_STREQ(retrieved, original);
}

TEST(StringHashDebugTests, GetStringForUnknownHash) {
	// Create a hash without registering it
	StringHash hash = HashString("never_registered");

	// Should return "<unknown>"
	const char* result = GetStringForHash(hash);
	EXPECT_STREQ(result, "<unknown>");
}

TEST(StringHashDebugTests, MultipleHashStringDebugCalls) {
	// Calling HashStringDebug multiple times with same string should not assert
	StringHash hash1 = HashStringDebug("repeated");
	StringHash hash2 = HashStringDebug("repeated");
	StringHash hash3 = HashStringDebug("repeated");

	EXPECT_EQ(hash1, hash2);
	EXPECT_EQ(hash2, hash3);
}

TEST(StringHashDebugTests, RegistryPersistsBetweenCalls) {
	// Register several hashes
	HashStringDebug("first");
	HashStringDebug("second");
	HashStringDebug("third");

	// All should be retrievable
	EXPECT_STREQ(GetStringForHash(HASH("first")), "first");
	EXPECT_STREQ(GetStringForHash(HASH("second")), "second");
	EXPECT_STREQ(GetStringForHash(HASH("third")), "third");
}

// Note: We can't easily test collision detection without finding two strings
// that actually collide with FNV-1a, which is extremely rare. The collision
// detection code path exists but is not exercised in these tests.

#endif // DEBUG

// ============================================================================
// Performance Considerations (Compile-time verification)
// ============================================================================

TEST(StringHashTests, SizeOfStringHash) {
	// Verify StringHash is 64-bit
	EXPECT_EQ(sizeof(StringHash), 8);
	EXPECT_EQ(sizeof(StringHash), sizeof(uint64_t));
}

TEST(StringHashTests, HashIsConstant) {
	// Verify hashing is deterministic
	const char* str = "deterministic_test";

	StringHash hash1 = HashString(str);
	StringHash hash2 = HashString(str);
	StringHash hash3 = HashString(str);

	EXPECT_EQ(hash1, hash2);
	EXPECT_EQ(hash2, hash3);
}

// ============================================================================
// Usage Pattern Tests
// ============================================================================

TEST(StringHashTests, SwitchCasePattern) {
	// Verify hash can be used in switch-like patterns
	auto getComponentType = [](const char* name) -> int {
		StringHash hash = HashString(name);

		if (hash == hashes::kTransform) {
			return 1;
		}
		if (hash == hashes::kPosition) {
			return 2;
		}
		if (hash == hashes::kVelocity) {
			return 3;
		}
		if (hash == hashes::kRenderable) {
			return 4;
		}

		return 0;
	};

	EXPECT_EQ(getComponentType("Transform"), 1);
	EXPECT_EQ(getComponentType("Position"), 2);
	EXPECT_EQ(getComponentType("Velocity"), 3);
	EXPECT_EQ(getComponentType("Renderable"), 4);
	EXPECT_EQ(getComponentType("Unknown"), 0);
}

TEST(StringHashTests, MapKeyUsage) {
	// Verify hash can be used as map key
	std::unordered_map<StringHash, int> hashMap;

	hashMap[HASH("key1")] = 100;
	hashMap[HASH("key2")] = 200;
	hashMap[HASH("key3")] = 300;

	EXPECT_EQ(hashMap[HASH("key1")], 100);
	EXPECT_EQ(hashMap[HASH("key2")], 200);
	EXPECT_EQ(hashMap[HASH("key3")], 300);

	// Unknown key should not exist
	EXPECT_EQ(hashMap.find(HASH("key4")), hashMap.end());
}
