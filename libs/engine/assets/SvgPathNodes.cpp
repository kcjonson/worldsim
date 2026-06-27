#include "SvgPathNodes.h"

#include <cctype>
#include <cstdlib>

#include <glm/glm.hpp>

namespace engine::assets {
	namespace {

		// Scan the next number starting at `i`, skipping leading whitespace/commas. Handles sign,
		// decimal, and exponent. Advances `i` past the number. Returns false when none is found.
		bool nextNumber(const std::string& s, size_t& i, float& out) {
			const size_t n = s.size();
			while (i < n && (std::isspace(static_cast<unsigned char>(s[i])) != 0 || s[i] == ',')) {
				++i;
			}
			if (i >= n) {
				return false;
			}
			const size_t start = i;
			if (s[i] == '+' || s[i] == '-') {
				++i;
			}
			bool anyDigit = false;
			while (i < n && std::isdigit(static_cast<unsigned char>(s[i])) != 0) {
				++i;
				anyDigit = true;
			}
			if (i < n && s[i] == '.') {
				++i;
				while (i < n && std::isdigit(static_cast<unsigned char>(s[i])) != 0) {
					++i;
					anyDigit = true;
				}
			}
			if (!anyDigit) {
				return false;
			}
			if (i < n && (s[i] == 'e' || s[i] == 'E')) {
				const size_t save = i;
				++i;
				if (i < n && (s[i] == '+' || s[i] == '-')) {
					++i;
				}
				bool expDigit = false;
				while (i < n && std::isdigit(static_cast<unsigned char>(s[i])) != 0) {
					++i;
					expDigit = true;
				}
				if (!expDigit) {
					i = save; // a trailing 'e' that wasn't an exponent
				}
			}
			out = std::strtof(s.c_str() + start, nullptr);
			return true;
		}

		bool isCommandChar(char c) {
			switch (c) {
				case 'M': case 'm': case 'L': case 'l': case 'H': case 'h':
				case 'V': case 'v': case 'C': case 'c': case 'S': case 's':
				case 'Q': case 'q': case 'T': case 't': case 'A': case 'a':
				case 'Z': case 'z':
					return true;
				default:
					return false;
			}
		}

	} // namespace

	std::vector<glm::vec2> parseSvgPathNodes(const std::string& d) {
		std::vector<glm::vec2> nodes;
		glm::vec2			   cur{0.0F, 0.0F};
		glm::vec2			   start{0.0F, 0.0F};
		char				   cmd = 0;
		size_t				   i = 0;
		const size_t		   n = d.size();

		auto num = [&](float& v) { return nextNumber(d, i, v); };

		while (i < n) {
			while (i < n && (std::isspace(static_cast<unsigned char>(d[i])) != 0 || d[i] == ',')) {
				++i;
			}
			if (i >= n) {
				break;
			}
			if (isCommandChar(d[i])) {
				cmd = d[i];
				++i;
			}
			if (cmd == 0) {
				break; // numbers before any command: malformed
			}

			const bool rel = std::islower(static_cast<unsigned char>(cmd)) != 0;
			const char up = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));

			float a = 0.0F, b = 0.0F, c = 0.0F, e = 0.0F, f = 0.0F, g = 0.0F, h = 0.0F;
			switch (up) {
				case 'Z':
					cur = start; // close; no new node
					continue;
				case 'M':
					if (!num(a) || !num(b)) {
						return nodes;
					}
					cur = rel ? cur + glm::vec2{a, b} : glm::vec2{a, b};
					start = cur;
					nodes.push_back(cur);
					cmd = rel ? 'l' : 'L'; // implicit repeats after M are line-to
					break;
				case 'L':
					if (!num(a) || !num(b)) {
						return nodes;
					}
					cur = rel ? cur + glm::vec2{a, b} : glm::vec2{a, b};
					nodes.push_back(cur);
					break;
				case 'H':
					if (!num(a)) {
						return nodes;
					}
					cur.x = rel ? cur.x + a : a;
					nodes.push_back(cur);
					break;
				case 'V':
					if (!num(a)) {
						return nodes;
					}
					cur.y = rel ? cur.y + a : a;
					nodes.push_back(cur);
					break;
				case 'C':
					if (!num(a) || !num(b) || !num(c) || !num(e) || !num(f) || !num(g)) {
						return nodes;
					}
					cur = rel ? cur + glm::vec2{f, g} : glm::vec2{f, g};
					nodes.push_back(cur);
					break;
				case 'S':
				case 'Q':
					if (!num(a) || !num(b) || !num(c) || !num(e)) {
						return nodes;
					}
					cur = rel ? cur + glm::vec2{c, e} : glm::vec2{c, e};
					nodes.push_back(cur);
					break;
				case 'T':
					if (!num(a) || !num(b)) {
						return nodes;
					}
					cur = rel ? cur + glm::vec2{a, b} : glm::vec2{a, b};
					nodes.push_back(cur);
					break;
				case 'A':
					if (!num(a) || !num(b) || !num(c) || !num(e) || !num(f) || !num(g) || !num(h)) {
						return nodes;
					}
					cur = rel ? cur + glm::vec2{g, h} : glm::vec2{g, h};
					nodes.push_back(cur);
					break;
				default:
					return nodes;
			}
		}
		return nodes;
	}

} // namespace engine::assets
