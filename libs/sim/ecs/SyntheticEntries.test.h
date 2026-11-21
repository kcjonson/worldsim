#pragma once

// Behavior library entries for tests, read by the real loader: a test writes its XML under a config root unique to the
// run (a random token, the test's name, and a count), beside the shipped fact tuning, and loads it with loadEntries.
// The root is removed when the entries go.

#include <assets/AssetRegistry.h>
#include <assets/EntryLoader.h>
#include <assets/ShippedEntries.test.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ecs::testing {

	class SyntheticEntries {
	  public:
		/// Where a definition an entry names is looked up.
		enum class Definitions : uint8_t {
			/// The definitions the test registered in the asset registry.
			Registry,
			/// The definitions the shipped asset folders declare.
			Shipped,
			/// The registry's, then the shipped folders' for a name the registry lacks.
			RegistryThenShipped,
		};

		struct Options {
			Definitions definitions = Definitions::Registry;
			/// The sim's fixed list of needs as need ids (EntrySources::trackedNeeds).
			std::vector<std::string> trackedNeeds;
			/// Shipped config folders (`behaviors`, `actions`) copied in beside what the test writes.
			std::vector<std::string> shippedFolders;
		};

		/// An empty config root holding the shipped fact tuning and the folders the loader reads, named for `purpose`.
		explicit SyntheticEntries(std::string_view purpose, Options options = {})
			: settings(std::move(options)) {
			static const std::string runToken = [] {
				std::random_device device;
				return std::to_string(device()) + "_" + std::to_string(device());
			}();
			static int	roots = 0;
			const auto* test = ::testing::UnitTest::GetInstance()->current_test_info();
			rootPath = std::filesystem::temp_directory_path() / ("worldsim-" + std::string(purpose) + "-" + runToken + "-" +
																 (test != nullptr ? test->name() : "") + "-" + std::to_string(++roots));
			std::filesystem::remove_all(rootPath);
			const std::filesystem::path shipped = assets::testing::shippedRepoRoot() / "assets" / "config";
			for (const char* folder : {"behaviors", "actions", "work", "orders", "facts"}) {
				std::filesystem::create_directories(rootPath / folder);
			}
			std::filesystem::copy(shipped / "facts", rootPath / "facts", std::filesystem::copy_options::recursive);
			for (const std::string& folder : settings.shippedFolders) {
				std::filesystem::copy(
					shipped / folder,
					rootPath / folder,
					std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing
				);
			}
		}

		~SyntheticEntries() {
			std::error_code ignored;
			std::filesystem::remove_all(rootPath, ignored);
		}

		SyntheticEntries(const SyntheticEntries&) = delete;
		SyntheticEntries& operator=(const SyntheticEntries&) = delete;

		[[nodiscard]] const std::filesystem::path& root() const { return rootPath; }

		/// Write `body` to `relative` under the root, its folders made as needed.
		void write(const std::string& relative, const std::string& body) const {
			std::filesystem::create_directories((rootPath / relative).parent_path());
			std::ofstream(rootPath / relative) << body;
		}

		/// Everything under the root, read by loadEntries.
		[[nodiscard]] assets::EntryLoad load() const {
			assets::EntrySources sources{.configRoot = rootPath, .trackedNeeds = settings.trackedNeeds};
			sources.definitionId = [definitions = settings.definitions](std::string_view name) -> std::optional<uint32_t> {
				if (definitions != Definitions::Shipped) {
					const assets::AssetRegistry& registry = assets::AssetRegistry::Get();
					const uint32_t				 definition = registry.getDefNameId(std::string(name));
					if (registry.getDefinitionById(definition) != nullptr) {
						return definition;
					}
				}
				if (definitions != Definitions::Registry) {
					const auto found = assets::testing::shippedDefinitionNames().find(name);
					if (found != assets::testing::shippedDefinitionNames().end()) {
						return found->second;
					}
				}
				return std::nullopt;
			};
			return assets::loadEntries(sources);
		}

	  private:
		Options				  settings;
		std::filesystem::path rootPath;
	};

} // namespace ecs::testing
