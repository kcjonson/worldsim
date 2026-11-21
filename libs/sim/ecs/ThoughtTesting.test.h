#pragma once

// Thought types for tests: the shipped condition vocabulary, and thought types written in the test as
// the body of a <thoughts> file, both loaded through the entry points the game's loadColonistRules uses, their
// action sources resolved against the shipped entries as the game's config load does.

#include "ColonistRules.h"
#include "ShippedConfig.test.h"
#include "ThoughtTypes.h"

#include <conditions/ConditionVocabulary.h>

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace ecs::testing {

	/// The tuning and feelings every test's types are written against.
	inline constexpr std::string_view kTestPreamble = R"(
  <tuning maxLineLength="40" maxSubjectNameLength="12" whileCheckMinutes="15">
    <kept limit="2000" pruneStartsAt="0.9" pruneStopsAt="0.8" recentDays="10" trivialWeight="1" normalWeight="3"/>
  </tuning>
  <feeling id="content" label="Content"/>
  <feeling id="delighted" label="Delighted"/>
  <feeling id="annoyed" label="Annoyed"/>
  <feeling id="miserable" label="Miserable"/>
  <feeling id="furious" label="Furious"/>
  <feeling id="resigned" label="Resigned"/>
  <feeling id="relieved" label="Relieved"/>
)";

	class TestThoughtTypes {
	  public:
		/// Loads kTestPreamble and `body` as the base file, then `laterBody`, when given, as a later file that
		/// may replace what the base declares. Load and build errors are kept in `errors`, not failed on.
		explicit TestThoughtTypes(std::string_view body, std::string_view laterBody = {})
			: types(vocabulary) {
			for (const std::string& error : loadConditionVocabulary(vocabulary, repoConfig() / "conditions", shippedTraits())) {
				ADD_FAILURE() << error;
			}

			static std::atomic<int>		nextFolder{0};
			const std::filesystem::path folder =
				std::filesystem::temp_directory_path() /
				("worldsim-thoughts-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + "-" +
				 std::to_string(nextFolder++));
			std::filesystem::remove_all(folder);
			std::filesystem::create_directories(folder / "mods");
			{
				std::ofstream file(folder / "thoughts.xml");
				file << "<thoughts>" << kTestPreamble << body << "</thoughts>";
			}
			if (!laterBody.empty()) {
				std::ofstream file(folder / "mods" / "later.xml");
				file << "<thoughts>" << laterBody << "</thoughts>";
			}
			errors = loadThoughtTypes(types, folder);
			if (errors.empty()) {
				errors = types.resolveActions(shippedEntries());
			}
			std::filesystem::remove_all(folder);
		}

		[[nodiscard]] ThoughtEventTypeId type(std::string_view name) const {
			const std::optional<ThoughtEventTypeId> found = types.findType(name);
			EXPECT_TRUE(found.has_value()) << name;
			return found.value_or(0);
		}

		[[nodiscard]] FeelingId feeling(std::string_view name) const {
			const std::optional<FeelingId> found = types.findFeeling(name);
			EXPECT_TRUE(found.has_value()) << name;
			return found.value_or(0);
		}

		conditions::ConditionVocabulary vocabulary;
		ThoughtTypes					types;
		std::vector<std::string>		errors;
	};

} // namespace ecs::testing
