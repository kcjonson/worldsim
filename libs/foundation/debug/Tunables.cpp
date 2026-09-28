#include "debug/Tunables.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace Foundation {

	Tunables& Tunables::instance() {
		static Tunables registry;
		return registry;
	}

	Tunables::Entry* Tunables::find(std::string_view name) {
		for (Entry& e : entryList) {
			if (e.name == name) {
				return &e;
			}
		}
		return nullptr;
	}

	const float* Tunables::add(std::string_view name, std::span<const float> defaults) {
		if (Entry* existing = find(name)) {
			return existing->value.data();
		}
		Entry& e = entryList.emplace_back();
		e.name	 = std::string(name);
		e.count	 = std::clamp<size_t>(defaults.size(), 1, kMaxComponents);
		std::copy_n(defaults.begin(), std::min(defaults.size(), kMaxComponents), e.defaultValue.begin());
		e.value = e.defaultValue;
		return e.value.data();
	}

	bool Tunables::set(std::string_view name, std::span<const float> values) {
		Entry* e = find(name);
		if (e == nullptr || values.size() != e->count) {
			return false;
		}
		if (std::any_of(values.begin(), values.end(), [](float v) { return !std::isfinite(v); })) {
			return false;
		}
		std::copy(values.begin(), values.end(), e->value.begin());
		return true;
	}

	bool Tunables::parseValues(std::string_view spec, std::vector<float>& out) {
		out.clear();
		if (spec.empty()) {
			return false;
		}
		// strtof needs a null-terminated buffer; spec may point into a non-owning view.
		const std::string owned(spec);
		const char*		  p = owned.c_str();
		while (true) {
			char*		end = nullptr;
			const float v = std::strtof(p, &end);
			if (end == p || (*end != '\0' && *end != ',') || !std::isfinite(v)) {
				out.clear();
				return false;
			}
			out.push_back(v);
			if (*end == '\0') {
				return true;
			}
			p = end + 1;
			if (*p == '\0') {
				out.clear(); // trailing comma
				return false;
			}
		}
	}

	size_t Tunables::reset(std::string_view name) {
		const bool			   wildcard = !name.empty() && name.back() == '*';
		const std::string_view prefix	= wildcard ? name.substr(0, name.size() - 1) : name;
		size_t				   count	= 0;
		for (Entry& e : entryList) {
			const bool match = wildcard ? std::string_view(e.name).starts_with(prefix) : e.name == name;
			if (match) {
				e.value = e.defaultValue;
				++count;
			}
		}
		return count;
	}

	std::string Tunables::toJson(std::string_view prefix) const {
		auto list = [](std::ostringstream& out, const std::array<float, kMaxComponents>& v, size_t n) {
			out << "[";
			for (size_t i = 0; i < n; ++i) {
				out << (i > 0 ? "," : "") << v[i];
			}
			out << "]";
		};

		std::ostringstream out;
		out << "{\"tunables\":[";
		bool first = true;
		for (const Entry& e : entryList) {
			if (!std::string_view(e.name).starts_with(prefix)) {
				continue;
			}
			out << (first ? "" : ",") << "{\"name\":\"" << e.name << "\",\"value\":";
			first = false;
			list(out, e.value, e.count);
			out << ",\"default\":";
			list(out, e.defaultValue, e.count);
			out << "}";
		}
		out << "]}";
		return out.str();
	}

} // namespace Foundation
