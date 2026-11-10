#include "assets/AssetValidator.h"

#include "assets/AssetDefinition.h"
#include "assets/GroundLayer.h"
#include "assets/GroundTint.h"
#include "assets/TreeDefSchema.h"

#include <world/chunk/Chunk.h>

#include <pugixml.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <unordered_map>

namespace assets {

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

		bool isSimpleAssetType(const std::string& s) {
			return s == "simple" || s == "Simple";
		}

		bool isGroundRole(const std::string& s) {
			return s == "ground" || s == "Ground";
		}

		bool isKnownRole(const std::string& s) {
			return s == "groundcover" || s == "Groundcover" || s == "worldobject" || s == "WorldObject" || isGroundRole(s);
		}

		bool isWholeNumber(double v) {
			return std::abs(v - std::round(v)) < 1e-6;
		}

		// The chunk edge in meters; a ground tile's period must divide it so tiles line up across chunks.
		constexpr double kChunkMeters = 512.0;
		constexpr int	 kMinGroundPixelsPerMeter = 16;
		constexpr int	 kMaxGroundPixelsPerMeter = 512;

		// Ground defs are baked into texture arrays, so their settings have hard limits (GroundLayer.h).
		// `surfaceToDef` carries the surfaces already claimed across files, to catch two defs painting one,
		// and `layerSizes` each layer size in use and the first def using it, for the bank's array count.
		void validateGroundDef(
			const pugi::xml_node&						  assetDef,
			const std::string&							  defName,
			std::unordered_map<std::string, std::string>& surfaceToDef,
			std::map<int, std::string>&					  layerSizes,
			ValidationReport&							  report
		) {
			const std::string surfaceName = assetDef.child("surface").text().as_string();
			const auto		  surface = world::surfaceFromString(surfaceName);
			if (!surface.has_value()) {
				report.add(Severity::Error, defName, "surface", "Ground def needs a <surface> naming a world surface", surfaceName);
			} else if (*surface == world::Surface::Water) {
				report.add(Severity::Error, defName, "surface", "Water is not a land surface; ground tiles paint land only", surfaceName);
			} else if (const auto claimed = surfaceToDef.emplace(surfaceName, defName); !claimed.second) {
				report.add(Severity::Error, defName, "surface", "Another ground def already paints this surface", claimed.first->second + " and " + defName);
			}

			// The baker draws a ground def through AssetRegistry::buildPaths, which only runs generators.
			// Loading an SVG per variant (with data-lod) is planned in docs/technical/ground-textures.md.
			if (isSimpleAssetType(assetDef.child("assetType").text().as_string())) {
				report.add(
					Severity::Error,
					defName,
					"assetType",
					"SVG ground defs aren't supported yet; use assetType procedural with a generator",
					"simple"
				);
			}

			const GroundParams defaults;

			const pugi::xml_node pixelsNode = assetDef.child("pixelsPerMeter");
			const int			 pixelsPerMeter = pixelsNode ? pixelsNode.text().as_int(0) : static_cast<int>(defaults.pixelsPerMeter);
			const bool			 pixelsValid = pixelsPerMeter >= kMinGroundPixelsPerMeter && pixelsPerMeter <= kMaxGroundPixelsPerMeter &&
									   (pixelsPerMeter & (pixelsPerMeter - 1)) == 0;
			if (!pixelsValid) {
				report.add(
					Severity::Error, defName, "pixelsPerMeter", "pixelsPerMeter must be a power of two from 16 to 512", pixelsNode.text().as_string()
				);
			}

			const pugi::xml_node periodNode = assetDef.child("period");
			const double		 period = periodNode ? periodNode.text().as_double(0.0) : static_cast<double>(defaults.periodMeters);
			const bool			 periodValid = period > 0.0 && isWholeNumber(kChunkMeters / period);
			if (!periodValid) {
				report.add(Severity::Error, defName, "period", "period must divide 512 (meters) exactly so tiles line up across chunks", periodNode.text().as_string());
			} else if (pixelsValid) {
				const GroundLayer layer = groundLayer(period, pixelsPerMeter);
				if (!layer.whole) {
					report.add(
						Severity::Error,
						defName,
						"period",
						"period x pixelsPerMeter must be a whole number of pixels",
						periodNode.text().as_string()
					);
				} else if (!layer.inRange) {
					report.add(
						Severity::Error,
						defName,
						"period",
						"period x pixelsPerMeter must be from 1 to " + std::to_string(kMaxGroundLayerSize) +
							" pixels; the baker takes no bigger layer",
						std::to_string(layer.texels) + " px at " + std::to_string(pixelsPerMeter) + " px/m"
					);
				} else {
					layerSizes.emplace(layer.texels, defName);
				}
			}

			const pugi::xml_node variantNode = assetDef.child("variantCount");
			if (variantNode) {
				if (!groundVariantCountSupported(variantNode.text().as_llong(0))) {
					report.add(
						Severity::Error,
						defName,
						"variantCount",
						"variantCount must be from 1 to " + std::to_string(kMaxGroundVariants) + " for a ground def",
						variantNode.text().as_string()
					);
				}
			}

			const std::string rotate = assetDef.child("rotate").text().as_string();
			if (!rotate.empty() && rotate != "free" && rotate != "Free" && rotate != "quarter" && rotate != "Quarter" && rotate != "none" &&
				rotate != "None" && rotate != "mirror" && rotate != "Mirror") {
				report.add(Severity::Warning, defName, "rotate", "Unknown rotate; the loader treats it as free", rotate);
			}

			if (assetDef.child("placement").child("biome")) {
				report.add(Severity::Error, defName, "placement", "Ground defs are never placed in the world; remove the <biome> rules");
			}

			std::vector<GroundMacroIssue> macroIssues;
			readGroundMacro(assetDef, macroIssues);
			for (GroundMacroIssue& issue : macroIssues) {
				report.add(Severity::Error, defName, "macro", "<" + issue.field + "> " + issue.message, std::move(issue.value));
			}

			const bool						   landSurface = surface.has_value() && *surface != world::Surface::Water;
			std::vector<GroundTransitionIssue> transitionIssues;
			readGroundTransitions(assetDef, landSurface ? surface : std::nullopt, transitionIssues);
			for (GroundTransitionIssue& issue : transitionIssues) {
				report.add(issue.error ? Severity::Error : Severity::Warning, defName, "transition", std::move(issue.message), std::move(issue.context));
			}
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

		// Surface name -> the ground def that paints it, across the whole tree.
		std::unordered_map<std::string, std::string> groundSurfaceToDef;

		// Ground layer size -> the first def baking layers of it.
		std::map<int, std::string> groundLayerSizes;

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

				if (isGroundRole(role)) {
					validateGroundDef(assetDef, defName, groundSurfaceToDef, groundLayerSizes, report);
				} else {
					for (const char* field : {"surface", "period", "pixelsPerMeter", "rotate", "macro", "seedGroup", "transition"}) {
						if (assetDef.child(field)) {
							report.add(Severity::Warning, defName, field, std::string("<") + field + "> is only read for role ground; it has no effect", fileCtx);
						}
					}
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

				std::vector<TreeDefIssue> treeIssues;
				readAgeClasses(assetDef, treeIssues);
				readPlacementScale(assetDef, treeIssues);
				for (TreeDefIssue& issue : treeIssues) {
					report.add(Severity::Error, defName, issue.field, std::move(issue.message), std::move(issue.value));
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

		// Every land surface needs a ground def: the tile shader has nothing else to paint it with.
		// Only checked once the tree has any ground content, so a library (or a test tree) with no
		// ground tiles at all stays quiet.
		if (!groundSurfaceToDef.empty()) {
			for (uint8_t i = 0; i < static_cast<uint8_t>(world::Surface::Count); ++i) {
				const auto surface = static_cast<world::Surface>(i);
				const std::string name = world::surfaceToString(surface);
				if (surface != world::Surface::Water && groundSurfaceToDef.find(name) == groundSurfaceToDef.end()) {
					report.add(Severity::Error, "", "surface", "No ground def paints this land surface", name);
				}
			}
		}

		// The bank keeps one texture array per layer size, and the tile shader samples at most kMaxGroundArrays.
		if (groundLayerSizes.size() > static_cast<size_t>(kMaxGroundArrays)) {
			std::string sizes;
			for (const auto& [texels, defName] : groundLayerSizes) {
				sizes += (sizes.empty() ? "" : ", ") + std::to_string(texels) + " px (" + defName + ")";
			}
			report.add(
				Severity::Error,
				"",
				"pixelsPerMeter",
				"The ground defs bake " + std::to_string(groundLayerSizes.size()) + " layer sizes; the tile shader holds " +
					std::to_string(kMaxGroundArrays) + ", one texture array each",
				sizes
			);
		}

		return report;
	}

} // namespace assets
