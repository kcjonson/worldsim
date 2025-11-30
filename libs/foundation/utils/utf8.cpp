#include "utils/utf8.h"
#include <cassert>

namespace foundation {
namespace UTF8 {

size_t characterSize(unsigned char firstByte) {
	// UTF-8 encoding:
	// 0xxxxxxx = 1 byte (ASCII)
	// 110xxxxx = 2 bytes
	// 1110xxxx = 3 bytes
	// 11110xxx = 4 bytes
	// 10xxxxxx = continuation byte (invalid as first byte)

	if ((firstByte & 0x80) == 0x00) {
		return 1; // 0xxxxxxx
	}
	if ((firstByte & 0xE0) == 0xC0) {
		return 2; // 110xxxxx
	}
	if ((firstByte & 0xF0) == 0xE0) {
		return 3; // 1110xxxx
	}
	if ((firstByte & 0xF8) == 0xF0) {
		return 4; // 11110xxx
	}

	// Invalid UTF-8 byte - treat as 1 byte
	return 1;
}

size_t previousCharacterSize(const std::string& str, size_t offset) {
	assert(offset > 0 && "previousCharacterSize: offset must be > 0");
	assert(offset <= str.size() && "previousCharacterSize: offset out of bounds");

	// Walk backwards from offset-1 until we find a non-continuation byte
	size_t pos = offset - 1;

	// Skip continuation bytes (10xxxxxx)
	size_t count = 1;
	while (pos > 0 && isContinuationByte(static_cast<unsigned char>(str[pos]))) {
		pos--;
		count++;
		if (count > 4) {
			// Invalid UTF-8 sequence (too many continuation bytes)
			return 1;
		}
	}

	// Verify the character size matches what the first byte indicates
	size_t expectedSize = characterSize(static_cast<unsigned char>(str[pos]));
	if (expectedSize != count) {
		// Invalid UTF-8 sequence - return 1 byte as fallback
		return 1;
	}

	return count;
}

std::string encode(char32_t codepoint) {
	std::string result;

	if (codepoint <= 0x7F) {
		// 1 byte: 0xxxxxxx
		result.push_back(static_cast<char>(codepoint));
	} else if (codepoint <= 0x7FF) {
		// 2 bytes: 110xxxxx 10xxxxxx
		result.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
		result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
	} else if (codepoint <= 0xFFFF) {
		// 3 bytes: 1110xxxx 10xxxxxx 10xxxxxx
		result.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
		result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
		result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
	} else if (codepoint <= 0x10FFFF) {
		// 4 bytes: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
		result.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
		result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
		result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
		result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
	} else {
		// Invalid codepoint - return replacement character U+FFFD
		result = "\xEF\xBF\xBD";
	}

	return result;
}

std::vector<char32_t> decode(const std::string& str) {
	std::vector<char32_t> result;
	result.reserve(str.size()); // Worst case: all ASCII

	size_t i = 0;
	while (i < str.size()) {
		unsigned char firstByte = static_cast<unsigned char>(str[i]);
		size_t		  charSize = characterSize(firstByte);

		// Check if we have enough bytes
		if (i + charSize > str.size()) {
			// Incomplete UTF-8 sequence - add replacement character
			result.push_back(0xFFFD);
			break;
		}

		char32_t codepoint = 0;

		if (charSize == 1) {
			// ASCII: 0xxxxxxx
			codepoint = firstByte;
		} else if (charSize == 2) {
			// 2 bytes: 110xxxxx 10xxxxxx
			codepoint = ((firstByte & 0x1F) << 6) | (static_cast<unsigned char>(str[i + 1]) & 0x3F);
		} else if (charSize == 3) {
			// 3 bytes: 1110xxxx 10xxxxxx 10xxxxxx
			codepoint = ((firstByte & 0x0F) << 12) | ((static_cast<unsigned char>(str[i + 1]) & 0x3F) << 6) |
						(static_cast<unsigned char>(str[i + 2]) & 0x3F);
		} else if (charSize == 4) {
			// 4 bytes: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
			codepoint = ((firstByte & 0x07) << 18) | ((static_cast<unsigned char>(str[i + 1]) & 0x3F) << 12) |
						((static_cast<unsigned char>(str[i + 2]) & 0x3F) << 6) |
						(static_cast<unsigned char>(str[i + 3]) & 0x3F);
		}

		result.push_back(codepoint);
		i += charSize;
	}

	return result;
}

size_t characterCount(const std::string& str) {
	size_t count = 0;
	size_t i = 0;

	while (i < str.size()) {
		size_t charSize = characterSize(static_cast<unsigned char>(str[i]));
		count++;
		i += charSize;
	}

	return count;
}

bool isContinuationByte(unsigned char byte) {
	// Continuation bytes: 10xxxxxx
	return (byte & 0xC0) == 0x80;
}

size_t nextCharacterBoundary(const std::string& str, size_t offset) {
	if (offset >= str.size()) {
		return str.size();
	}

	size_t charSize = characterSize(static_cast<unsigned char>(str[offset]));
	return std::min(offset + charSize, str.size());
}

size_t previousCharacterBoundary(const std::string& str, size_t offset) {
	if (offset == 0) {
		return 0;
	}

	size_t charSize = previousCharacterSize(str, offset);
	return offset - charSize;
}

} // namespace UTF8
} // namespace foundation
