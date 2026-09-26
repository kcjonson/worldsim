#pragma once

// Tunables - named float and color values the debug server reads and sets live,
// so a look can be A/B'd without a rebuild. A consumer registers each value once
// under a slash-grouped name ("terrain/shore/drySandW") with its default, keeps
// the returned pointer, and reads through it every frame. The registry owns the
// storage, so every consumer of a name shares one value and it survives the
// consumer being recreated (a scene reload keeps a live-tuned look).
//
// Game-thread only: the app applies /api/dev/tunable and serves
// /api/state?what=tunables on the game thread, where consumers read.

#include <array>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Foundation { // NOLINT(readability-identifier-naming)

	class Tunables {
	  public:
		/// Components per value: 1 for a scalar, 3 for an RGB color.
		static constexpr size_t kMaxComponents = 3;

		struct Entry {
			std::string							 name;
			size_t								 count = 1;
			std::array<float, kMaxComponents> value{};
			std::array<float, kMaxComponents> defaultValue{};
		};

		/// The process-wide registry.
		static Tunables& instance();

		/// Register `name` with `defaults` (1 or kMaxComponents floats) and return
		/// its storage. A name registered before keeps its current value and
		/// storage; registering it again with a different component count is a
		/// programming error and returns the existing storage unchanged.
		const float* add(std::string_view name, std::span<const float> defaults);

		/// Set a registered value. False if the name is unknown, the component
		/// count doesn't match, or any value isn't finite.
		bool set(std::string_view name, std::span<const float> values);

		/// Parses a dev-command value spec ("1.6", "0.2,0.3,0.4") into `out`.
		/// Every comma-separated token must be a whole finite number: no empty
		/// tokens, no leading/trailing garbage after the digits, no trailing
		/// comma, no nan/inf. False (and `out` cleared) on any malformed token.
		static bool parseValues(std::string_view spec, std::vector<float>& out);

		/// Restore one value to its default, or every value whose name starts
		/// with `prefix` when `name` ends in '*' ("terrain/shore/*", "*"). Returns
		/// how many were reset.
		size_t reset(std::string_view name);

		/// JSON: {"tunables":[{"name":..,"value":[..],"default":[..]},..]} for
		/// every name starting with `prefix`, in registration order.
		[[nodiscard]] std::string toJson(std::string_view prefix = {}) const;

		[[nodiscard]] const std::deque<Entry>& entries() const { return entryList; }

	  private:
		Entry* find(std::string_view name);

		// A deque never moves its elements on push_back, so the pointers add()
		// hands out stay valid for the process lifetime.
		std::deque<Entry> entryList;
	};

} // namespace Foundation
