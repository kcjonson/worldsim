#include "scenes/game/dev/DevJson.h"

#include <cstdio>

namespace world_sim {

	std::string jsonEscape(const std::string& text) {
		std::string out;
		out.reserve(text.size() + 8);
		for (char c : text) {
			switch (c) {
				case '"': out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\b': out += "\\b"; break;
				case '\f': out += "\\f"; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default:
					if (static_cast<unsigned char>(c) < 0x20) {
						// Other control chars: emit a \u00XX escape rather than dropping them.
						char buf[7];
						std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
						out += buf;
					} else {
						out += c;
					}
			}
		}
		return out;
	}

	std::string jsonNumber(double value) {
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%.3f", value);
		return buf;
	}

} // namespace world_sim
