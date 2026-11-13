#pragma once

// TaskRowText - a task row's status and detail line worked out from plain facts, kept apart from the
// adapter so world-sim-tests can link them without the ECS.

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace world_sim::adapters {

	/// One or two names joined with ", "; more as the first two then "+N" ("Alice, Bob +2"); "" for none.
	[[nodiscard]] std::string joinNames(std::span<const std::string> names);

	/// "needs 12, 4 on the way", leaving out a part that is zero; "" when both are.
	[[nodiscard]] std::string quantityText(uint32_t needed, uint32_t onTheWay);

	/// A blocked craft's materials for its station's next job.
	struct CraftMaterials {
		uint32_t landed = 0;
		uint32_t required = 0;
	};

	/// What a goal row's status and detail are read from, gathered by the adapter.
	struct TaskRowFacts {
		bool  buildSite = false;		 // a construction umbrella: its status comes from the site
		bool  underConstruction = false; // build site: materials are in
		int	  buildPercent = 0;			 // build site
		bool  blocked = false;			 // goal status Blocked
		bool  noSource = false;			 // goal status NoSource
		bool  allSourcesClaimed = false;
		float distance = 0.0F;					   // meters from the reference point
		std::optional<CraftMaterials> materials; // a blocked craft's
		uint32_t					  needed = 0;	   // beyond what is on its way
		uint32_t					  onTheWay = 0;
		std::string					  workers; // joined names of who works it, "" for none
		std::string					  knownTo; // joined names of who was offered work toward it
	};

	struct TaskRowText {
		std::string status;
		std::string detail;
		bool		blocked = false;
		bool		unassigned = false;
	};

	/// The row's status, by precedence Blocked, Waiting (no source found), Working, Waiting (all known
	/// sources claimed), then Available (further than 50m) or Unassigned; a build site reads Needs
	/// materials, Building, or Ready to build. The detail joins with "; " the status's own part, the
	/// quantities, and, while nobody works it, "known to" whoever was offered work toward it.
	[[nodiscard]] TaskRowText taskRowText(const TaskRowFacts& facts);

} // namespace world_sim::adapters
