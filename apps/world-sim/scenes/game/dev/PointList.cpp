#include "scenes/game/dev/PointList.h"

#include <cstdlib>
#include <sstream>

namespace world_sim {

	std::vector<Foundation::Vec2> parsePointList(const std::string& spec) {
		std::vector<Foundation::Vec2> pts;
		std::stringstream			  ss(spec);
		std::string					  pair;
		while (std::getline(ss, pair, ';')) {
			const auto comma = pair.find(',');
			if (comma == std::string::npos) {
				continue;
			}
			const std::string xs = pair.substr(0, comma);
			const std::string ys = pair.substr(comma + 1);
			char*			  endX = nullptr;
			char*			  endY = nullptr;
			const float		  x = std::strtof(xs.c_str(), &endX);
			const float		  y = std::strtof(ys.c_str(), &endY);
			// Skip the pair unless both parses consumed at least one character; strtof returns 0.0
			// on a non-numeric string, which would otherwise silently inject a (0,0) vertex.
			if (endX == xs.c_str() || endY == ys.c_str()) {
				continue;
			}
			pts.emplace_back(x, y);
		}
		return pts;
	}

	std::string pointCountRefusal(const std::string& verb, std::size_t least, std::size_t got) {
		if (got >= least) {
			return {};
		}
		return verb + ": pts needs at least " + std::to_string(least) + " 'x,y' pairs (got " + std::to_string(got) + ")";
	}

} // namespace world_sim
