#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace foundation {

/**
 * UTF8 Utility Functions
 *
 * Provides utilities for working with UTF-8 encoded strings in std::string.
 * Used by TextInput for cursor positioning and text editing.
 *
 * Key Concepts:
 * - std::string stores UTF-8 bytes (1-4 bytes per character)
 * - Cursor positions and offsets are BYTE offsets, not character counts
 * - These utilities handle multi-byte character boundaries
 */
namespace UTF8 {

	/**
	 * Get the byte size of the UTF-8 character starting at the given byte.
	 *
	 * @param firstByte - The first byte of the UTF-8 character
	 * @return Character size in bytes (1-4)
	 *
	 * Example:
	 *   'A' (0x41) → 1 byte
	 *   'é' (0xC3 0xA9) → 2 bytes
	 *   '世' (0xE4 0xB8 0x96) → 3 bytes
	 *   '😀' (0xF0 0x9F 0x98 0x80) → 4 bytes
	 */
	size_t CharacterSize(unsigned char firstByte);

	/**
	 * Get the byte size of the UTF-8 character immediately before the given offset.
	 *
	 * @param str - The UTF-8 string
	 * @param offset - Byte offset in string (must be > 0)
	 * @return Size of the previous character in bytes (1-4)
	 *
	 * Example:
	 *   "Hello😀" at offset 8 → returns 4 (emoji is 4 bytes)
	 *   "Hello😀" at offset 5 → returns 1 ('o' is 1 byte)
	 */
	size_t PreviousCharacterSize(const std::string& str, size_t offset);

	/**
	 * Encode a Unicode codepoint to UTF-8 string.
	 *
	 * @param codepoint - Unicode codepoint (U+0000 to U+10FFFF)
	 * @return UTF-8 encoded string (1-4 bytes)
	 *
	 * Example:
	 *   U+0041 ('A') → "A" (1 byte)
	 *   U+00E9 ('é') → "\xC3\xA9" (2 bytes)
	 *   U+1F600 ('😀') → "\xF0\x9F\x98\x80" (4 bytes)
	 */
	std::string Encode(char32_t codepoint);

	/**
	 * Decode a UTF-8 string to a vector of Unicode codepoints.
	 *
	 * @param str - UTF-8 encoded string
	 * @return Vector of Unicode codepoints
	 *
	 * Example:
	 *   "Hello😀" → {U+0048, U+0065, U+006C, U+006C, U+006F, U+1F600}
	 *
	 * Note: Invalid UTF-8 sequences are replaced with U+FFFD (replacement character)
	 */
	std::vector<char32_t> Decode(const std::string& str);

	/**
	 * Count the number of UTF-8 characters in a string (not bytes).
	 *
	 * @param str - UTF-8 encoded string
	 * @return Number of characters (not bytes)
	 *
	 * Example:
	 *   "Hello" → 5 characters (5 bytes)
	 *   "Hello😀" → 6 characters (9 bytes)
	 */
	size_t CharacterCount(const std::string& str);

	/**
	 * Check if a byte is a valid UTF-8 continuation byte (10xxxxxx).
	 *
	 * @param byte - Byte to check
	 * @return True if byte is a continuation byte
	 */
	bool IsContinuationByte(unsigned char byte);

	/**
	 * Find the byte offset of the next character boundary.
	 *
	 * @param str - UTF-8 string
	 * @param offset - Current byte offset
	 * @return Byte offset of next character (offset + 1-4 bytes)
	 *
	 * Returns str.size() if at end of string.
	 */
	size_t NextCharacterBoundary(const std::string& str, size_t offset);

	/**
	 * Find the byte offset of the previous character boundary.
	 *
	 * @param str - UTF-8 string
	 * @param offset - Current byte offset
	 * @return Byte offset of previous character (offset - 1-4 bytes)
	 *
	 * Returns 0 if at start of string.
	 */
	size_t PreviousCharacterBoundary(const std::string& str, size_t offset);

} // namespace UTF8

} // namespace foundation
