#pragma once

// Test helpers that wait for AssetRegistry::beginLoadAsync to finish.
//
// There is no deadline. The load runs every Lua generator in the shipped library,
// which a Debug build on a slow CI runner can stretch well past the old 10 s budget,
// and that budget couldn't tell a slow runner from a hang. The worker always ends by
// setting done (a throw terminates the process), so a load that never finishes is a
// hang, caught by the TIMEOUT on the assets-tests ctest entry. Every kHeartbeat the
// wait prints how many definitions have loaded, so a timed-out log shows whether the
// load was still advancing.

#include "assets/AssetRegistry.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace assets {

	inline void waitForAsyncLoad(const AssetRegistry& reg) {
		using Clock = std::chrono::steady_clock;
		constexpr auto kHeartbeat = std::chrono::seconds(30);

		const auto start = Clock::now();
		auto	   nextHeartbeat = start + kHeartbeat;
		while (!reg.isLoadComplete()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			const auto now = Clock::now();
			if (now >= nextHeartbeat) {
				const auto secs = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
				printf(
					"[AssetRegistry] still loading after %lld s: %d definitions\n",
					static_cast<long long>(secs),
					reg.loadProgress().defsLoaded.load()
				);
				fflush(stdout);
				nextHeartbeat += kHeartbeat;
			}
		}
	}

	// A throwaway asset folder in the temp directory, removed with it. Each def goes in
	// <Name>/<Name>.xml, the layout loadDefinitionsFromFolder reads.
	class TempAssetFolder {
	  public:
		TempAssetFolder() {
			static std::atomic<int> counter{0};
			root = std::filesystem::temp_directory_path() /
				   ("asset_folder_" + std::to_string(reinterpret_cast<uintptr_t>(&counter)) + "_" + std::to_string(counter++));
			std::filesystem::create_directories(root);
		}
		~TempAssetFolder() {
			std::error_code ec;
			std::filesystem::remove_all(root, ec);
		}
		TempAssetFolder(const TempAssetFolder&) = delete;
		TempAssetFolder& operator=(const TempAssetFolder&) = delete;

		void write(const std::string& relPath, const std::string& content) const {
			const std::filesystem::path p = root / relPath;
			std::filesystem::create_directories(p.parent_path());
			std::ofstream(p) << content;
		}

		std::filesystem::path root;
	};

	// Loads the shipped library (<root>/assets/world, with its shared scripts) through
	// beginLoadAsync and waits for it. False when the library isn't there to load.
	inline bool loadShippedLibrary(AssetRegistry& reg) {
		// <root>/libs/assets/assets/AssetRegistry.test.h -> walk up to <root>.
		const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
		const std::filesystem::path world = root / "assets" / "world";
		if (!std::filesystem::exists(world)) {
			return false;
		}
		reg.setSharedScriptsPath(root / "assets" / "shared" / "scripts");
		reg.beginLoadAsync(world.string());
		waitForAsyncLoad(reg);
		return true;
	}

} // namespace assets
