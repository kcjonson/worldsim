#pragma once

// Thought types for tests: the shipped condition vocabulary, and thought types written in the test as
// the body of a <thoughts> file, loaded through the same loader and registry the game uses.

#include "ThoughtTypes.h"

#include <conditions/ConditionVocabulary.h>
#include <conditions/SimTerms.h>

#include <assets/ConditionConfig.h>
#include <assets/ThoughtConfig.h>

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace ecs::testing {

	inline std::filesystem::path repoConfig() {
		return std::filesystem::path(SIM_TEST_REPO_DIR) / "assets" / "config";
	}

	/// The tuning and feelings every test's types are written against.
	inline constexpr std::string_view kTestPreamble = R"(
  <tuning maxLineLength="40" whileCheckMinutes="15"/>
  <feeling id="content" icon="feeling_content" expression="smile"/>
  <feeling id="delighted" icon="feeling_delighted" expression="grin"/>
  <feeling id="annoyed" icon="feeling_annoyed" expression="frown"/>
  <feeling id="miserable" icon="feeling_miserable" expression="droop"/>
  <feeling id="furious" icon="feeling_furious" expression="scowl"/>
  <feeling id="resigned" icon="feeling_resigned" expression="sigh"/>
  <feeling id="relieved" icon="feeling_relieved" expression="exhale"/>
)";

	class TestThoughtTypes {
	  public:
		/// Loads kTestPreamble and `body`. Load and build errors are kept in `errors`, not failed on.
		explicit TestThoughtTypes(std::string_view body)
			: types(vocabulary) {
			const assets::ConditionConfigLoad conditionConfig = assets::loadConditionConfig(repoConfig() / "conditions");
			for (const std::string& error : conditionConfig.errors) {
				ADD_FAILURE() << error;
			}
			for (const std::string& error : conditions::buildSimVocabulary(vocabulary, conditionConfig.config)) {
				ADD_FAILURE() << error;
			}

			static std::atomic<int>		nextFolder{0};
			const std::filesystem::path folder =
				std::filesystem::temp_directory_path() /
				("worldsim-thoughts-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + "-" +
				 std::to_string(nextFolder++));
			std::filesystem::remove_all(folder);
			std::filesystem::create_directories(folder);
			{
				std::ofstream file(folder / "thoughts.xml");
				file << "<thoughts>" << kTestPreamble << body << "</thoughts>";
			}
			const assets::ThoughtConfigLoad load = assets::loadThoughtConfig(folder);
			std::filesystem::remove_all(folder);
			errors = load.errors;
			if (errors.empty()) {
				errors = types.load(load.config);
			}
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
