#include "scenes/game/dev/DevJson.h"

#include <cmath>
#include <cstdio>

namespace world_sim {

	namespace {

		constexpr unsigned char kFirstPrintableCharacter = 0x20;
		constexpr int			kEscapeHexDigits = 4;
		constexpr std::size_t	kEscapeBufferSize = 7; // backslash, u, four hex digits, terminator
		constexpr int			kNumberDecimals = 3;
		constexpr std::size_t	kNumberBufferSize = 32;

	} // namespace

	std::string jsonEscape(const std::string& text) {
		std::string out;
		out.reserve(text.size() + 8);
		for (const char character : text) {
			switch (character) {
				case '"':
					out += "\\\"";
					break;
				case '\\':
					out += "\\\\";
					break;
				case '\b':
					out += "\\b";
					break;
				case '\f':
					out += "\\f";
					break;
				case '\n':
					out += "\\n";
					break;
				case '\r':
					out += "\\r";
					break;
				case '\t':
					out += "\\t";
					break;
				default:
					if (static_cast<unsigned char>(character) < kFirstPrintableCharacter) {
						// Other control chars: emit a \u00XX escape rather than dropping them.
						char escape[kEscapeBufferSize];
						std::snprintf(escape, sizeof(escape), "\\u%0*x", kEscapeHexDigits, static_cast<unsigned char>(character));
						out += escape;
					} else {
						out += character;
					}
			}
		}
		return out;
	}

	std::string jsonStrings(const std::vector<std::string>& texts) {
		std::string out = "[";
		for (const std::string& text : texts) {
			if (out.size() > 1) {
				out += ',';
			}
			out += '"' + jsonEscape(text) + '"';
		}
		return out + "]";
	}

	std::string jsonNumber(double value) {
		if (!std::isfinite(value)) {
			return "null";
		}
		char text[kNumberBufferSize];
		std::snprintf(text, sizeof(text), "%.*f", kNumberDecimals, value);
		return text;
	}

} // namespace world_sim
