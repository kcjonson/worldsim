#include "assets/AssetValidator.h"

#include <pugixml.hpp>

#include <algorithm>
#include <set>
#include <string>
#include <unordered_map>

namespace engine::assets {

	namespace fs = std::filesystem;

	void ValidationReport::add(Severity severity, std::string defName, std::string field, std::string message, std::string context) {
		issues.push_back({severity, std::move(defName), std::move(field), std::move(message), std::move(context)});
	}

	int ValidationReport::errorCount() const {
		return static_cast<int>(std::count_if(issues.begin(), issues.end(), [](const ValidationIssue& i) { return i.severity == Severity::Error; }));
	}

	int ValidationReport::warningCount() const {
		return static_cast<int>(std::count_if(issues.begin(), issues.end(), [](const ValidationIssue& i) { return i.severity == Severity::Warning; }));
	}

	bool ValidationReport::hasErrors() const {
		return errorCount() > 0;
	}

	namespace {

		bool isKnownAssetType(const std::string& s) {
			return s == "simple" || s == "Simple" || s == "procedural" || s == "Procedural";
		}

		bool isKnownRole(const std::string& s) {
			return s == "groundcover" || s == "Groundcover" || s == "worldobject" || s == "WorldObject";
		}

		// Resolve a generator script path the way AssetRegistry::generateAsset does.
		// Returns an empty path if @shared/ is used without a configured shared path.
		fs::path resolveScriptPath(const std::string& scriptPath, const fs::path& assetFolder, const fs::path& sharedScriptsPath) {
			static const std::string kSharedPrefix = "@shared/";
			if (scriptPath.compare(0, kSharedPrefix.size(), kSharedPrefix) == 0) {
				if (sharedScriptsPath.empty()) {
					return {};
				}
				return sharedScriptsPath / scriptPath.substr(kSharedPrefix.size());
			}
			return assetFolder / scriptPath;
		}

	} // namespace

	ValidationReport AssetValidator::validate(const fs::path& assetsRoot, const fs::path& sharedScriptsPath) {
		ValidationReport report;

		// Each filesystem probe gets its own error_code so a transient failure on
		// one path can't poison traversal or later checks.
		const auto pathExists = [](const fs::path& p) {
			std::error_code e;
			return fs::exists(p, e);
		};

		std::error_code rootEc;
		if (!fs::is_directory(assetsRoot, rootEc)) {
			report.add(Severity::Error, "", "", "Assets root not found or not a directory", assetsRoot.string());
			return report;
		}

		// defName -> file it was first declared in, for cross-library duplicate detection.
		std::unordered_map<std::string, std::string> defNameToFile;

		std::error_code						   iterEc;
		fs::recursive_directory_iterator	   it(assetsRoot, iterEc);
		const fs::recursive_directory_iterator end;
		for (; it != end; it.increment(iterEc)) {
			if (iterEc) {
				report.add(Severity::Warning, "", "", "Stopped scanning after a filesystem error", iterEc.message());
				break;
			}

			const fs::directory_entry& entry = *it;
			std::error_code			   entryEc;
			if (!entry.is_regular_file(entryEc) || entry.path().extension() != ".xml") {
				continue;
			}

			const std::string stem = entry.path().stem().string();
			const std::string parentFolder = entry.path().parent_path().filename().string();
			if (stem != parentFolder) {
				report.add(Severity::Warning, "", "", "XML filename does not match its folder; the loader skips it", entry.path().string());
				continue;
			}

			const fs::path	  assetFolder = entry.path().parent_path();
			const std::string fileCtx = entry.path().string();

			pugi::xml_document	   doc;
			pugi::xml_parse_result parsed = doc.load_file(entry.path().string().c_str());
			if (!parsed) {
				report.add(Severity::Error, "", "", std::string("XML parse error: ") + parsed.description(), fileCtx);
				continue;
			}
			pugi::xml_node root = doc.child("AssetDefinitions");
			if (!root) {
				report.add(Severity::Error, "", "", "Missing <AssetDefinitions> root element", fileCtx);
				continue;
			}

			// SVGs referenced by this folder's defs, to find orphans afterward.
			std::set<std::string> referencedSvgs;

			for (pugi::xml_node assetDef : root.children("AssetDef")) {
				const std::string defName = assetDef.child("defName").text().as_string();
				if (defName.empty()) {
					report.add(Severity::Error, "", "defName", "AssetDef is missing a defName", fileCtx);
					continue;
				}

				// Duplicate defName: the loader silently overwrites (last wins).
				auto existing = defNameToFile.find(defName);
				if (existing != defNameToFile.end()) {
					report.add(Severity::Error, defName, "defName", "Duplicate defName (loader silently overwrites)", existing->second + " and " + fileCtx);
				} else {
					defNameToFile.emplace(defName, fileCtx);
				}

				// Unknown assetType silently becomes Procedural.
				const std::string assetType = assetDef.child("assetType").text().as_string();
				if (!assetType.empty() && !isKnownAssetType(assetType)) {
					report.add(Severity::Warning, defName, "assetType", "Unknown assetType; the loader treats it as Procedural", assetType);
				}

				// Unknown role silently becomes WorldObject. (Distinct from <category>,
				// which is the inventory ItemCategory.)
				const std::string role = assetDef.child("role").text().as_string();
				if (!role.empty() && !isKnownRole(role)) {
					report.add(Severity::Warning, defName, "role", "Unknown role; the loader treats it as WorldObject", role);
				}

				// svgPath must resolve to a real file.
				const std::string svgPath = assetDef.child("svgPath").text().as_string();
				if (!svgPath.empty()) {
					const fs::path resolved = (assetFolder / svgPath).lexically_normal();
					referencedSvgs.insert(resolved.string());
					if (!pathExists(resolved)) {
						report.add(Severity::Error, defName, "svgPath", "Referenced SVG does not exist", resolved.string());
					}
				}

				// Generator script must resolve to a real file.
				const std::string scriptPath = assetDef.child("generator").child("scriptPath").text().as_string();
				if (!scriptPath.empty()) {
					const fs::path resolved = resolveScriptPath(scriptPath, assetFolder, sharedScriptsPath);
					if (resolved.empty()) {
						report.add(Severity::Error, defName, "scriptPath", "@shared/ used but no shared scripts path is configured", scriptPath);
					} else if (!pathExists(resolved)) {
						report.add(Severity::Error, defName, "scriptPath", "Generator script does not exist", resolved.string());
					}
				}

				// Documented-but-ignored fields (the parser never reads these).
				if (assetDef.child("variation")) {
					report.add(Severity::Warning, defName, "variation", "<variation> is documented but unimplemented; it has no effect", fileCtx);
				}
				if (assetDef.attribute("ParentDef") || assetDef.child("ParentDef")) {
					report.add(Severity::Warning, defName, "ParentDef", "Definition inheritance is documented but unimplemented; it has no effect", fileCtx);
				}
				if (assetDef.child("components") || assetDef.child("variant")) {
					report.add(Severity::Warning, defName, "components", "Component/variant SVG references are documented but unimplemented; they have no effect", fileCtx);
				}

				// variantCount nested in <rendering> is ignored (parser reads only a
				// top-level <variantCount>).
				if (assetDef.child("rendering").child("variantCount")) {
					report.add(Severity::Warning, defName, "variantCount", "variantCount inside <rendering> is ignored; move it to a top-level <variantCount>", fileCtx);
				}
			}

			// Orphan SVGs: .svg files in the folder that no definition references.
			std::error_code				 dirEc;
			fs::directory_iterator		 dit(assetFolder, dirEc);
			const fs::directory_iterator dend;
			for (; dit != dend; dit.increment(dirEc)) {
				if (dirEc) {
					break;
				}
				const fs::directory_entry& f = *dit;
				std::error_code			   fEc;
				if (!f.is_regular_file(fEc) || f.path().extension() != ".svg") {
					continue;
				}
				const std::string canon = f.path().lexically_normal().string();
				if (referencedSvgs.find(canon) == referencedSvgs.end()) {
					report.add(Severity::Warning, "", "", "SVG file is not referenced by any definition (orphan)", f.path().string());
				}
			}
		}

		return report;
	}

} // namespace engine::assets
