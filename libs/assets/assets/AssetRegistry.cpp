// Asset Registry Implementation
// Handles XML parsing with pugixml and asset generation.

#include "assets/AssetRegistry.h"
#include "assets/GroundSvg.h"
#include "assets/SvgPathNodes.h"
#include "assets/TreeDefSchema.h"
#include "assets/VariantSeed.h"
#include "assets/lua/LuaGenerator.h"
#include "assets/placement/FloraRouting.h"

#include <silhouette/Silhouette.h>
#include <threading/TaskPool.h>
#include <utils/Log.h>
#include <vector/MeshBounds.h>
#include <vector/SVGLoader.h>
#include <world/chunk/Chunk.h>

#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <sstream>

namespace assets {

	namespace {
		/// Pack a definition's present capabilities into the bitmask the interning index stores.
		/// 16-bit: there are 9 capability types and Storage is bit 8, which overflows a uint8_t.
		[[nodiscard]] uint16_t computeCapabilityMask(const AssetDefinition& def) {
			uint16_t mask = 0;
			if (def.capabilities.edible.has_value()) {
				mask |= static_cast<uint16_t>(1U << static_cast<uint8_t>(CapabilityType::Edible));
			}
			if (def.capabilities.drinkable.has_value()) {
				mask |= static_cast<uint16_t>(1U << static_cast<uint8_t>(CapabilityType::Drinkable));
			}
			if (def.capabilities.sleepable.has_value()) {
				mask |= static_cast<uint16_t>(1U << static_cast<uint8_t>(CapabilityType::Sleepable));
			}
			if (def.capabilities.toilet.has_value()) {
				mask |= static_cast<uint16_t>(1U << static_cast<uint8_t>(CapabilityType::Toilet));
			}
			if (def.capabilities.waste.has_value()) {
				mask |= static_cast<uint16_t>(1U << static_cast<uint8_t>(CapabilityType::Waste));
			}
			if (def.capabilities.carryable.has_value()) {
				mask |= static_cast<uint16_t>(1U << static_cast<uint8_t>(CapabilityType::Carryable));
			}
			if (def.capabilities.harvestable.has_value()) {
				mask |= static_cast<uint16_t>(1U << static_cast<uint8_t>(CapabilityType::Harvestable));
			}
			if (def.capabilities.craftable.has_value()) {
				mask |= static_cast<uint16_t>(1U << static_cast<uint8_t>(CapabilityType::Craftable));
			}
			if (def.capabilities.storage.has_value()) {
				mask |= static_cast<uint16_t>(1U << static_cast<uint8_t>(CapabilityType::Storage));
			}
			return mask;
		}
	} // namespace

	// ============================================================================
	// GeneratorParams Implementation
	// ============================================================================

	std::string GeneratorParams::getString(const char* key, const std::string& defaultVal) const {
		auto it = params.find(key);
		if (it != params.end()) {
			return it->second;
		}
		return defaultVal;
	}

	float GeneratorParams::getFloat(const char* key, float defaultVal) const {
		auto it = params.find(key);
		if (it != params.end()) {
			try {
				return std::stof(it->second);
			} catch (...) {
				return defaultVal;
			}
		}
		return defaultVal;
	}

	void GeneratorParams::getFloatRange(const char* key, float& outMin, float& outMax, float defaultMin, float defaultMax) const {
		outMin = defaultMin;
		outMax = defaultMax;

		auto it = params.find(key);
		if (it == params.end()) {
			return;
		}

		const auto& str = it->second;
		auto		commaPos = str.find(',');
		if (commaPos == std::string::npos) {
			// Single value - use for both min and max
			try {
				float val = std::stof(str);
				outMin = val;
				outMax = val;
			} catch (...) {
				// Keep defaults
			}
			return;
		}

		// Parse min,max format
		try {
			outMin = std::stof(str.substr(0, commaPos));
			outMax = std::stof(str.substr(commaPos + 1));
		} catch (...) {
			outMin = defaultMin;
			outMax = defaultMax;
		}
	}

	int32_t GeneratorParams::getInt(const char* key, int32_t defaultVal) const {
		auto it = params.find(key);
		if (it != params.end()) {
			try {
				return std::stoi(it->second);
			} catch (...) {
				return defaultVal;
			}
		}
		return defaultVal;
	}

	void GeneratorParams::setString(const char* key, const std::string& value) {
		params[key] = value;
	}

	void GeneratorParams::setFloat(const char* key, float value) {
		params[key] = std::to_string(value);
	}

	bool GeneratorParams::has(const char* key) const {
		return params.find(key) != params.end();
	}

	std::vector<std::pair<std::string, std::string>> GeneratorParams::sorted() const {
		std::vector<std::pair<std::string, std::string>> out(params.begin(), params.end());
		std::sort(out.begin(), out.end());
		return out;
	}

	// ============================================================================
	// GeneratorRegistry Implementation
	// ============================================================================

	GeneratorRegistry& GeneratorRegistry::Get() {
		static GeneratorRegistry instance;
		return instance;
	}

	void GeneratorRegistry::registerGenerator(const char* name, GeneratorFactory factory) {
		factories[name] = std::move(factory);
		LOG_DEBUG(Engine, "Registered generator: %s", name);
	}

	std::unique_ptr<IAssetGenerator> GeneratorRegistry::create(const char* name) {
		auto it = factories.find(name);
		if (it != factories.end()) {
			return it->second();
		}
		LOG_WARNING(Engine, "Generator not found: %s", name);
		return nullptr;
	}

	bool GeneratorRegistry::hasGenerator(const char* name) const {
		return factories.find(name) != factories.end();
	}

	// ============================================================================
	// AssetRegistry Implementation
	// ============================================================================

	AssetRegistry& AssetRegistry::Get() {
		static AssetRegistry instance;
		return instance;
	}

	namespace {

		AssetType parseAssetType(const char* str) {
			if (str == nullptr) {
				return AssetType::Procedural;
			}
			std::string s(str);
			if (s == "simple" || s == "Simple") {
				return AssetType::Simple;
			}
			return AssetType::Procedural;
		}

		AssetComplexity parseComplexity(const char* str) {
			if (str == nullptr) {
				return AssetComplexity::Simple;
			}
			std::string s(str);
			if (s == "complex" || s == "Complex") {
				return AssetComplexity::Complex;
			}
			return AssetComplexity::Simple;
		}

		AssetRole parseAssetRole(const char* str) {
			if (str == nullptr) {
				return AssetRole::WorldObject;
			}
			std::string s(str);
			if (s == "groundcover" || s == "Groundcover") {
				return AssetRole::Groundcover;
			}
			if (s == "ground" || s == "Ground") {
				return AssetRole::Ground;
			}
			return AssetRole::WorldObject;
		}

		AnimationType parseAnimationType(const char* str) {
			if (str == nullptr) {
				return AnimationType::None;
			}
			std::string s(str);
			if (s == "parametric" || s == "Parametric") {
				return AnimationType::Parametric;
			}
			if (s == "bezier" || s == "BezierDeform") {
				return AnimationType::BezierDeform;
			}
			return AnimationType::None;
		}

		Distribution parseDistribution(const char* str) {
			if (str == nullptr) {
				return Distribution::Uniform;
			}
			std::string s(str);
			if (s == "clumped" || s == "Clumped") {
				return Distribution::Clumped;
			}
			if (s == "spaced" || s == "Spaced") {
				return Distribution::Spaced;
			}
			return Distribution::Uniform;
		}

		RelationshipKind parseRelationshipKind(const char* name) {
			if (name == nullptr) {
				return RelationshipKind::Affinity;
			}
			std::string s(name);
			if (s == "requires") {
				return RelationshipKind::Requires;
			}
			if (s == "affinity") {
				return RelationshipKind::Affinity;
			}
			if (s == "avoids") {
				return RelationshipKind::Avoids;
			}
			return RelationshipKind::Affinity;
		}

		CapabilityQuality parseCapabilityQuality(const char* str) {
			if (str == nullptr) {
				return CapabilityQuality::Normal;
			}
			std::string s(str);
			if (s == "terrible" || s == "Terrible") {
				return CapabilityQuality::Terrible;
			}
			if (s == "poor" || s == "Poor") {
				return CapabilityQuality::Poor;
			}
			if (s == "good" || s == "Good") {
				return CapabilityQuality::Good;
			}
			if (s == "excellent" || s == "Excellent") {
				return CapabilityQuality::Excellent;
			}
			// "normal", "Normal", "clean", "Clean" all map to Normal
			return CapabilityQuality::Normal;
		}

		/// Parse "min,max" format into two integers
		void parseIntRange(const std::string& str, int32_t& outMin, int32_t& outMax, int32_t defaultMin, int32_t defaultMax) {
			outMin = defaultMin;
			outMax = defaultMax;
			if (str.empty()) {
				return;
			}
			auto commaPos = str.find(',');
			if (commaPos != std::string::npos) {
				try {
					outMin = std::stoi(str.substr(0, commaPos));
					outMax = std::stoi(str.substr(commaPos + 1));
				} catch (...) {
					// Keep defaults
				}
			} else {
				try {
					outMin = std::stoi(str);
					outMax = outMin;
				} catch (...) {
					// Keep defaults
				}
			}
			// Ensure min <= max for std::uniform_int_distribution
			if (outMin > outMax) {
				std::swap(outMin, outMax);
			}
		}

		/// Parse "min,max" format into two floats
		void parseFloatRange(const std::string& str, float& outMin, float& outMax, float defaultMin, float defaultMax) {
			outMin = defaultMin;
			outMax = defaultMax;
			if (str.empty()) {
				return;
			}
			auto commaPos = str.find(',');
			if (commaPos != std::string::npos) {
				try {
					outMin = std::stof(str.substr(0, commaPos));
					outMax = std::stof(str.substr(commaPos + 1));
				} catch (...) {
					// Keep defaults
				}
			} else {
				try {
					outMin = std::stof(str);
					outMax = outMin;
				} catch (...) {
					// Keep defaults
				}
			}
			// Ensure min <= max for std::uniform_real_distribution
			if (outMin > outMax) {
				std::swap(outMin, outMax);
			}
		}

		/// Parse a "x,y" pair into a glm::vec2. Returns false on malformed input.
		bool parseVec2(const std::string& str, glm::vec2& out) {
			auto commaPos = str.find(',');
			if (commaPos == std::string::npos) {
				return false;
			}
			try {
				out.x = std::stof(str.substr(0, commaPos));
				out.y = std::stof(str.substr(commaPos + 1));
			} catch (...) {
				return false;
			}
			return true;
		}

		constexpr uint32_t kGroundDefaultVariantCount = 8;

		/// Read a ground def's elements into def.ground. Missing or malformed ones keep their
		/// defaults (the validator reports them as issues; here they only log).
		void parseGroundParams(const pugi::xml_node& defNode, AssetDefinition& def) {
			GroundParams& ground = def.ground;

			const std::string surfaceName = defNode.child_value("surface");
			ground.surface				  = world::surfaceFromString(surfaceName);
			if (!ground.surface.has_value()) {
				LOG_WARNING(Engine, "Ground AssetDef '%s' has no valid <surface> ('%s')", def.defName.c_str(), surfaceName.c_str());
			}

			ground.periodMeters	  = defNode.child("period").text().as_float(ground.periodMeters);
			ground.pixelsPerMeter = defNode.child("pixelsPerMeter").text().as_uint(ground.pixelsPerMeter);
			ground.seedGroup	  = defNode.child_value("seedGroup");
			if (ground.seedGroup.empty()) {
				ground.seedGroup = def.defName;
			}

			const std::string rotate = defNode.child_value("rotate");
			if (rotate == "free" || rotate == "Free") {
				ground.rotate = GroundRotate::Free;
			} else if (rotate == "quarter" || rotate == "Quarter") {
				ground.rotate = GroundRotate::Quarter;
			} else if (rotate == "none" || rotate == "None") {
				ground.rotate = GroundRotate::None;
			} else if (rotate == "mirror" || rotate == "Mirror") {
				ground.rotate = GroundRotate::Mirror;
			} else if (!rotate.empty()) {
				LOG_WARNING(Engine, "Ground AssetDef '%s' has unknown <rotate> '%s'; using free", def.defName.c_str(), rotate.c_str());
			}

			std::vector<GroundTransitionIssue> transitionIssues;
			ground.transitions = readGroundTransitions(defNode, ground.surface, transitionIssues);
			for (const GroundTransitionIssue& issue : transitionIssues) {
				LOG_WARNING(Engine, "Ground AssetDef '%s' <transition>: %s (%s)", def.defName.c_str(), issue.message.c_str(), issue.context.c_str());
			}

			std::vector<GroundMacroIssue> macroIssues;
			ground.macro = readGroundMacro(defNode, macroIssues);
			for (const GroundMacroIssue& issue : macroIssues) {
				LOG_WARNING(
					Engine,
					"Ground AssetDef '%s' <macro><%s> %s ('%s'); keeping the default",
					def.defName.c_str(),
					issue.field.c_str(),
					issue.message.c_str(),
					issue.value.c_str()
				);
			}
		}

		/// The one mesh in `meshes`, for callers that can only draw one. Logs and returns false when
		/// the asset needed several.
		bool takeSingleMesh(const std::string& defName, std::vector<renderer::TessellatedMesh>& meshes, renderer::TessellatedMesh& out) {
			if (meshes.size() != 1) {
				LOG_ERROR(
					Engine,
					"%s needs %zu meshes (a mesh holds at most %zu vertices); only buildMeshes can return it",
					defName.c_str(),
					meshes.size(),
					TessellatedPaths::kMaxMeshVertices
				);
				return false;
			}
			out = std::move(meshes.front());
			return true;
		}

		/// A ground path that set no height takes its paint-order rank, from 1 / n for the first of n paths
		/// to 1 for the last, so where only those paint, the topmost path at a texel is its height and
		/// a texel nothing covers, 0, is the lowest.
		void rankUnsetHeights(GeneratedAsset& asset) {
			const float n = static_cast<float>(asset.paths.size());
			for (size_t i = 0; i < asset.paths.size(); ++i) {
				GeneratedPath& path = asset.paths[i];
				if (!path.height.has_value()) {
					path.height = static_cast<float>(i + 1) / n;
				}
			}
		}

		/// Repeat every path of a ground tile that crosses an edge of its [0, period] square at the
		/// offsets it crosses into, right after the original. Paint order then holds across the
		/// seam, and the meshes clipped to the square are the seamless tile: drawn once, not 3x3.
		void wrapGroundPaths(GeneratedAsset& asset, float period) {
			// Shifts along one axis that bring a [low, high] span back across the square; [0] is the original.
			const auto shifts = [period](float low, float high, std::array<float, 3>& out) {
				size_t count = 1;
				if (low < 0.0F && high > 0.0F) {
					out[count++] = period;
				}
				if (low < period && high > period) {
					out[count++] = -period;
				}
				return count;
			};

			std::vector<GeneratedPath> wrapped;
			wrapped.reserve(asset.paths.size() + (asset.paths.size() / 4));
			std::vector<GeneratedPath> copies;
			for (GeneratedPath& path : asset.paths) {
				Foundation::Vec2 lo{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
				Foundation::Vec2 hi{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
				for (const Foundation::Vec2& v : path.vertices) {
					lo = {std::min(lo.x, v.x), std::min(lo.y, v.y)};
					hi = {std::max(hi.x, v.x), std::max(hi.y, v.y)};
				}
				std::array<float, 3> dx{};
				std::array<float, 3> dy{};
				const size_t		 nx = shifts(lo.x, hi.x, dx);
				const size_t		 ny = shifts(lo.y, hi.y, dy);

				copies.clear();
				for (size_t ix = 0; ix < nx; ++ix) {
					for (size_t iy = 0; iy < ny; ++iy) {
						if (ix == 0 && iy == 0) {
							continue;
						}
						GeneratedPath& copy = copies.emplace_back(path);
						for (Foundation::Vec2& v : copy.vertices) {
							v = {v.x + dx[ix], v.y + dy[iy]};
						}
					}
				}
				wrapped.push_back(std::move(path));
				std::move(copies.begin(), copies.end(), std::back_inserter(wrapped));
			}
			asset.paths = std::move(wrapped);
		}

		/// The SVG of an SVG ground def's variant that `seed` picks (seed modulo their count), read as the paths a
		/// generator draws. Logs and returns false when the file can't be read or breaks a ground SVG rule, which
		/// the validator reports too.
		bool readGroundSvgVariant(const AssetDefinition& def, uint32_t seed, GeneratedAsset& out) {
			if (def.svgPaths.empty()) {
				LOG_ERROR(Engine, "Ground AssetDef '%s' is an SVG def with no <svgPath>", def.defName.c_str());
				return false;
			}
			const std::string file = def.resolvePath(def.svgPaths[seed % def.svgPaths.size()]).string();
			std::ifstream	  in(file, std::ios::binary);
			if (!in) {
				LOG_ERROR(Engine, "Ground AssetDef '%s' can't read its SVG %s", def.defName.c_str(), file.c_str());
				return false;
			}
			const std::string		 bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
			std::vector<std::string> issues;
			out = readGroundSvg(
				{.svg = std::as_bytes(std::span(bytes)),
				 .periodMeters = def.ground.periodMeters,
				 .pixelsPerMeter = def.ground.pixelsPerMeter,
				 .name = file},
				issues
			);
			for (const std::string& issue : issues) {
				LOG_ERROR(Engine, "Ground AssetDef '%s' %s: %s", def.defName.c_str(), file.c_str(), issue.c_str());
			}
			return issues.empty();
		}

		/// SVG curve-flattening tolerance. Shared by every loadSVG call so the render
		/// path and the collision path flatten identically.
		constexpr float kSvgCurveTolerance = 0.5F;

		/// The SVG-unit -> world-meter frame shared by the render path (getTemplate
		/// scales the mesh by scaleFactor) and the collision path
		/// (loadCollisionFromSvgMetadata maps metadata points into the local-meter
		/// frame). They MUST agree or the collider drifts off the sprite, so the vertex
		/// bbox, scaleFactor (height-normalized), and center live here once. `center` is
		/// the full vertex-bbox center -- DynamicEntityRenderSystem centers the sprite on
		/// the entity using it, so the collision frame's origin matches. NB: the bbox is
		/// over path vertices; an asymmetric stroked outline could shift the render center
		/// by up to half the stroke width, so keep colliders fill-only.
		struct SvgMeterFrame {
			bool	  valid		  = false;
			float	  scaleFactor = 1.0F;
			glm::vec2 center{0.0F, 0.0F};
		};

		SvgMeterFrame computeSvgMeterFrame(const std::vector<renderer::LoadedSVGShape>& shapes, float worldHeight) {
			float minX = std::numeric_limits<float>::max();
			float maxX = std::numeric_limits<float>::lowest();
			float minY = std::numeric_limits<float>::max();
			float maxY = std::numeric_limits<float>::lowest();
			for (const auto& shape : shapes) {
				for (const auto& svgPath : shape.paths) {
					for (const auto& v : svgPath.vertices) {
						minX = std::min(minX, v.x);
						maxX = std::max(maxX, v.x);
						minY = std::min(minY, v.y);
						maxY = std::max(maxY, v.y);
					}
				}
			}
			const float svgHeight = maxY - minY;
			if (svgHeight <= 0.001F) {
				return {};
			}
			SvgMeterFrame frame;
			frame.valid		  = true;
			frame.scaleFactor = worldHeight / svgHeight;
			frame.center	  = {(minX + maxX) * 0.5F, (minY + maxY) * 0.5F};
			return frame;
		}

		// --- Motion (animation) parsing helpers -------------------------------------------------

		MotionChannel parseMotionChannel(const std::string& s) {
			if (s == "posX") {
				return MotionChannel::PosX;
			}
			if (s == "posY") {
				return MotionChannel::PosY;
			}
			if (s == "scaleX") {
				return MotionChannel::ScaleX;
			}
			if (s == "scaleY") {
				return MotionChannel::ScaleY;
			}
			return MotionChannel::Rotation;
		}

		MotionWave parseMotionWave(const std::string& s) { return (s == "triangle") ? MotionWave::Triangle : MotionWave::Sine; }

		// Parse a <Motion>/<clip>/<drive> file into unresolved drivers (pivots not yet bound).
		bool parseMotionXml(const std::string& path, MotionDef& out) {
			pugi::xml_document doc;
			if (!doc.load_file(path.c_str())) {
				return false;
			}
			pugi::xml_node root = doc.child("Motion");
			if (!root) {
				root = doc.first_child();
			}
			if (!root) {
				return false;
			}
			for (pugi::xml_node clipNode : root.children("clip")) {
				MotionClip clip;
				clip.name	= clipNode.attribute("name").as_string();
				clip.stride = clipNode.attribute("stride").as_float(1.0F);
				for (pugi::xml_node drvNode : clipNode.children("drive")) {
					MotionDriver d;
					d.part		  = drvNode.attribute("part").as_string();
					d.node		  = drvNode.attribute("node").as_int(0);
					d.channel	  = parseMotionChannel(drvNode.attribute("channel").as_string("rotation"));
					d.wave		  = parseMotionWave(drvNode.attribute("fn").as_string("sine"));
					d.amp		  = drvNode.attribute("amp").as_float(0.0F);
					d.freq		  = drvNode.attribute("freq").as_float(1.0F);
					d.phaseOffset = drvNode.attribute("phase").as_float(0.0F);
					clip.drivers.push_back(d);
				}
				out.clips.push_back(std::move(clip));
			}
			return !out.clips.empty();
		}

		// First <path> in a subtree, in document order ("" if none).
		std::string firstPathInSubtree(const pugi::xml_node& node) {
			for (pugi::xml_node child : node.children()) {
				if (std::string(child.name()) == "path") {
					return child.attribute("d").value();
				}
				std::string d = firstPathInSubtree(child);
				if (!d.empty()) {
					return d;
				}
			}
			return {};
		}

		// The `d` of the part identified by `partId`: if a <g id=partId>, its first descendant
		// <path>'s d; if a <path id=partId>, its own d. "" when not found.
		std::string findPartFirstPathD(const pugi::xml_node& node, const std::string& partId) {
			for (pugi::xml_node child : node.children()) {
				const pugi::xml_attribute idAttr = child.attribute("id");
				if (idAttr && partId == idAttr.value()) {
					if (std::string(child.name()) == "path") {
						return child.attribute("d").value();
					}
					std::string d = firstPathInSubtree(child);
					if (!d.empty()) {
						return d;
					}
				}
				std::string d = findPartFirstPathD(child, partId);
				if (!d.empty()) {
					return d;
				}
			}
			return {};
		}

	} // namespace

	std::optional<CollisionShape> AssetRegistry::loadCollisionFromSvgMetadata(const std::string& absoluteSvgPath, float worldHeight) {
		// Cheap first pass: NanoSVG ignores <metadata>, so read it ourselves with
		// pugixml. Bail before the (relatively costly) loadSVG when there's nothing
		// to convert.
		pugi::xml_document	   svgDoc;
		pugi::xml_parse_result svgResult = svgDoc.load_file(absoluteSvgPath.c_str());
		if (!svgResult) {
			return std::nullopt;
		}

		pugi::xml_node collisionNode = svgDoc.child("svg").child("metadata").child("collision");
		if (!collisionNode) {
			return std::nullopt;
		}

		pugi::xml_node shapeNode = collisionNode.child("shape");
		if (!shapeNode) {
			LOG_WARNING(Engine, "SVG collision metadata in '%s' has no <shape>; ignoring", absoluteSvgPath.c_str());
			return std::nullopt;
		}

		// Metadata is present: now load the SVG to derive the same vertex bbox the
		// render path uses, so SVG user units convert to meters identically.
		std::vector<renderer::LoadedSVGShape> shapes;
		if (!renderer::loadSVG(absoluteSvgPath, kSvgCurveTolerance, shapes)) {
			LOG_WARNING(Engine, "SVG collision metadata present but loadSVG failed for '%s'; ignoring", absoluteSvgPath.c_str());
			return std::nullopt;
		}

		const SvgMeterFrame frame = computeSvgMeterFrame(shapes, worldHeight);
		if (!frame.valid) {
			LOG_WARNING(Engine, "SVG '%s' has zero-height vertex bbox; ignoring collision metadata", absoluteSvgPath.c_str());
			return std::nullopt;
		}

		// Convert an authored SVG point into the collision local-meter frame, the same
		// frame the procedural collider uses (entity.position == scaled-mesh bbox center).
		auto toLocalMeters = [&](const glm::vec2& p) -> glm::vec2 { return (p - frame.center) * frame.scaleFactor; };

		const std::string type = shapeNode.attribute("type").as_string();

		if (type == "aabb") {
			glm::vec2 svgMin;
			glm::vec2 svgMax;
			if (!parseVec2(shapeNode.attribute("min").as_string(), svgMin) ||
				!parseVec2(shapeNode.attribute("max").as_string(), svgMax)) {
				LOG_WARNING(Engine, "SVG collision <shape type=\"aabb\"> in '%s' has malformed min/max; ignoring", absoluteSvgPath.c_str());
				return std::nullopt;
			}

			const glm::vec2 localMin = toLocalMeters(svgMin);
			const glm::vec2 localMax = toLocalMeters(svgMax);

			CollisionShape shape;
			shape.type			   = CollisionShapeType::Rect;
			shape.offsetMeters	   = (localMin + localMax) * 0.5F;
			shape.halfExtentsMeters = {std::abs(localMax.x - localMin.x) * 0.5F, std::abs(localMax.y - localMin.y) * 0.5F};

			if (shape.halfExtentsMeters.x <= 0.0F || shape.halfExtentsMeters.y <= 0.0F) {
				LOG_WARNING(Engine, "SVG collision aabb in '%s' is degenerate (zero extent); ignoring", absoluteSvgPath.c_str());
				return std::nullopt;
			}
			return shape;
		}

		if (type == "polygon") {
			CollisionShape	  shape;
			std::stringstream ss(shapeNode.attribute("vertices").as_string());
			std::string		  token;
			while (ss >> token) {
				glm::vec2 pt;
				if (!parseVec2(token, pt)) {
					LOG_WARNING(Engine, "SVG collision polygon in '%s' has malformed vertex '%s'; ignoring", absoluteSvgPath.c_str(), token.c_str());
					return std::nullopt;
				}
				shape.pointsMeters.push_back(toLocalMeters(pt));
			}
			if (shape.pointsMeters.size() < 3) {
				LOG_WARNING(Engine, "SVG collision polygon in '%s' has fewer than 3 points; ignoring", absoluteSvgPath.c_str());
				return std::nullopt;
			}
			shape.type = CollisionShapeType::Polygon;
			return shape;
		}

		LOG_WARNING(Engine, "SVG collision <shape> in '%s' has unknown type '%s'; ignoring", absoluteSvgPath.c_str(), type.c_str());
		return std::nullopt;
	}

	void AssetRegistry::setSharedScriptsPath(const std::filesystem::path& path) {
		m_sharedScriptsPath = path;
		LOG_DEBUG(Engine, "Set shared scripts path: %s", path.string().c_str());
	}

	bool AssetRegistry::loadDefinitions(const std::string& xmlPath) {
		namespace fs = std::filesystem;

		pugi::xml_document	   doc;
		pugi::xml_parse_result result = doc.load_file(xmlPath.c_str());

		// Extract base folder from XML path for relative path resolution
		// Use fs::absolute() to ensure baseFolder is always an absolute path
		fs::path baseFolder = fs::absolute(xmlPath).parent_path();

		if (!result) {
			LOG_ERROR(Engine, "Failed to load XML: %s - %s", xmlPath.c_str(), result.description());
			return false;
		}

		pugi::xml_node root = doc.child("AssetDefinitions");
		if (!root) {
			LOG_ERROR(Engine, "Missing <AssetDefinitions> root element in %s", xmlPath.c_str());
			return false;
		}

		int loadedCount = 0;
		std::vector<std::string> loadedNames;
		for (pugi::xml_node defNode : root.children("AssetDef")) {
			AssetDefinition def;

			// Required fields
			def.defName = defNode.child_value("defName");
			if (def.defName.empty()) {
				LOG_WARNING(Engine, "Skipping asset definition with empty defName");
				continue;
			}

			def.label = defNode.child_value("label");
			if (def.label.empty()) {
				def.label = def.defName;
			}

			// Asset type and render/placement category
			def.assetType = parseAssetType(defNode.child_value("assetType"));
			def.role = parseAssetRole(defNode.child_value("role"));

			// Generator (for procedural assets)
			pugi::xml_node genNode = defNode.child("generator");
			if (genNode) {
				def.generatorName = genNode.child_value("name");
				def.scriptPath = genNode.child_value("scriptPath");

				// Parse generator parameters
				pugi::xml_node paramsNode = genNode.child("params");
				if (paramsNode) {
					for (pugi::xml_node param : paramsNode.children()) {
						def.params.setString(param.name(), param.child_value());
					}
				}
			}

			// SVG paths and world height (for simple assets)
			for (const pugi::xml_node svgNode : defNode.children("svgPath")) {
				def.svgPaths.emplace_back(svgNode.child_value());
			}
			def.motionPath = defNode.child_value("motion"); // optional animation clips file
			def.worldHeight = static_cast<float>(defNode.child("worldHeight").text().as_double(1.0));

			// Random placement rotation (XML in degrees -> radians). Default 0 = upright.
			constexpr float kDegToRad = 0.01745329252F;
			def.maxRandomRotation =
				static_cast<float>(defNode.child("randomRotation").text().as_double(0.0)) * kDegToRad;

			// Rendering settings
			pugi::xml_node renderNode = defNode.child("rendering");
			if (renderNode) {
				def.complexity = parseComplexity(renderNode.child_value("complexity"));
			}

			// Animation settings
			pugi::xml_node animNode = defNode.child("animation");
			if (animNode) {
				def.animation.enabled = true;
				def.animation.type = parseAnimationType(animNode.child_value("type"));
				def.animation.windResponse = static_cast<float>(animNode.child("windResponse").text().as_double(0.3));

				// Parse sway frequency range
				std::string swayStr = animNode.child_value("swayFrequency");
				if (!swayStr.empty()) {
					auto commaPos = swayStr.find(',');
					if (commaPos != std::string::npos) {
						try {
							def.animation.swayFrequencyMin = std::stof(swayStr.substr(0, commaPos));
							def.animation.swayFrequencyMax = std::stof(swayStr.substr(commaPos + 1));
						} catch (...) {
							// Keep defaults
						}
					} else {
						try {
							def.animation.swayFrequencyMin = std::stof(swayStr);
							def.animation.swayFrequencyMax = def.animation.swayFrequencyMin;
						} catch (...) {
							// Keep defaults
						}
					}
				}
			}

			// Placement settings - per-biome configuration
			pugi::xml_node placementNode = defNode.child("placement");
			if (placementNode) {
				// Parse per-biome placement configs
				for (pugi::xml_node biomeNode : placementNode.children("biome")) {
					BiomePlacement bp;

					// Biome name (required)
					bp.biomeName = biomeNode.attribute("name").as_string();
					if (bp.biomeName.empty()) {
						LOG_WARNING(Engine, "Skipping biome placement with empty name");
						continue;
					}

					// Spawn chance
					bp.spawnChance = static_cast<float>(biomeNode.child("spawnChance").text().as_double(0.3));

					// Distribution type
					bp.distribution = parseDistribution(biomeNode.child_value("distribution"));

					// Clumping parameters (for Distribution::Clumped)
					pugi::xml_node clumpingNode = biomeNode.child("clumping");
					if (clumpingNode) {
						parseIntRange(clumpingNode.child_value("clumpSize"), bp.clumping.clumpSizeMin, bp.clumping.clumpSizeMax, 3, 12);
						parseFloatRange(
							clumpingNode.child_value("clumpRadius"), bp.clumping.clumpRadiusMin, bp.clumping.clumpRadiusMax, 0.5F, 2.0F
						);
						parseFloatRange(
							clumpingNode.child_value("clumpSpacing"), bp.clumping.clumpSpacingMin, bp.clumping.clumpSpacingMax, 3.0F, 8.0F
						);
					}

					// Spacing parameters (for Distribution::Spaced)
					pugi::xml_node spacingNode = biomeNode.child("spacing");
					if (spacingNode) {
						bp.spacing.minDistance = static_cast<float>(spacingNode.child("minDistance").text().as_double(2.0));
					}

					// Tile-type proximity (e.g., <biome name="Wetland" near="Water" distance="2">)
					bp.nearTileType = biomeNode.attribute("near").as_string();
					bp.nearDistance = biomeNode.attribute("distance").as_float(0.0F);

					def.placement.biomes.push_back(std::move(bp));
				}

				// Parse groups (self-declared group membership)
				pugi::xml_node groupsNode = placementNode.child("groups");
				if (groupsNode) {
					for (pugi::xml_node groupNode : groupsNode.children("group")) {
						const char* groupName = groupNode.text().as_string();
						if (groupName != nullptr && groupName[0] != '\0') {
							def.placement.groups.push_back(groupName);
						}
					}
				}

				// Parse relationships (entity-to-entity spawn rules)
				pugi::xml_node relationshipsNode = placementNode.child("relationships");
				if (relationshipsNode) {
					for (pugi::xml_node relNode : relationshipsNode.children()) {
						PlacementRelationship rel;
						rel.kind = parseRelationshipKind(relNode.name());
						rel.distance = relNode.attribute("distance").as_float(5.0F);

						// Parse target (defName, group, or type="same")
						if (relNode.attribute("defName")) {
							rel.target.type = EntityRef::Type::DefName;
							rel.target.value = relNode.attribute("defName").as_string();
						} else if (relNode.attribute("group")) {
							rel.target.type = EntityRef::Type::Group;
							rel.target.value = relNode.attribute("group").as_string();
						} else if (std::string(relNode.attribute("type").as_string()) == "same") {
							rel.target.type = EntityRef::Type::Same;
						}

						// Kind-specific attributes
						if (rel.kind == RelationshipKind::Affinity) {
							rel.strength = relNode.attribute("strength").as_float(1.5F);
						} else if (rel.kind == RelationshipKind::Avoids) {
							rel.penalty = relNode.attribute("penalty").as_float(0.5F);
						} else if (rel.kind == RelationshipKind::Requires) {
							std::string effect = relNode.attribute("effect").as_string();
							rel.required = (effect == "required");
						}

						def.placement.relationships.push_back(rel);
					}
				}
			}

			// Variant count; an SVG ground def's SVG list is its variant list
			const bool isGround = def.role == AssetRole::Ground;
			def.variantCount = static_cast<uint32_t>(defNode.child("variantCount").text().as_uint(isGround ? kGroundDefaultVariantCount : 1));
			if (isGround && def.assetType == AssetType::Simple) {
				def.variantCount = static_cast<uint32_t>(def.svgPaths.size());
			}
			if (isGround) {
				parseGroundParams(defNode, def);
			}

			std::vector<TreeDefIssue> treeIssues;
			def.ageClasses = readAgeClasses(defNode, treeIssues);
			if (const std::optional<PlacementScale> scale = readPlacementScale(defNode, treeIssues); scale.has_value()) {
				def.placement.scale = *scale;
			}
			for (const TreeDefIssue& issue : treeIssues) {
				LOG_WARNING(
					Engine,
					"AssetDef '%s' <%s> %s ('%s'); ignoring the block",
					def.defName.c_str(),
					issue.field.c_str(),
					issue.message.c_str(),
					issue.value.c_str()
				);
			}

			// Item category (for storage matching and UI grouping)
			std::string categoryStr = defNode.child_value("category");
			if (!categoryStr.empty()) {
				if (categoryStr == "RawMaterial") {
					def.category = ItemCategory::RawMaterial;
				} else if (categoryStr == "Food") {
					def.category = ItemCategory::Food;
				} else if (categoryStr == "Tool") {
					def.category = ItemCategory::Tool;
				} else if (categoryStr == "Furniture") {
					def.category = ItemCategory::Furniture;
				} else {
					LOG_WARNING(Engine, "Unknown item category '%s' in %s", categoryStr.c_str(), def.defName.c_str());
				}
			}

			// Hands required to carry (default 1, use 2 for large items like furniture)
			def.handsRequired = static_cast<uint8_t>(defNode.child("handsRequired").text().as_uint(1));

			// Tool type this item provides (e.g. "Axe" enables chopping); empty = not a tool
			def.toolType = defNode.child_value("toolType");

			// Item properties (for entities that can be carried/stored)
			pugi::xml_node itemNode = defNode.child("item");
			if (itemNode) {
				ItemProperties itemProps;
				itemProps.stackSize = itemNode.child("stackSize").text().as_uint(1);
				itemProps.massKg = itemNode.child("mass").text().as_float(1.0F);

				// Parse edible capability within item
				pugi::xml_node edibleNode = itemNode.child("edible");
				if (edibleNode) {
					EdibleCapability edible;
					edible.nutrition = edibleNode.attribute("nutrition").as_float(0.3F);
					edible.quality = parseCapabilityQuality(edibleNode.attribute("quality").as_string());
					edible.spoilable = edibleNode.attribute("spoilable").as_bool(false);
					itemProps.edible = edible;
				}

				def.itemProperties = itemProps;
			}

			// Capabilities - what actions can be performed on/with this entity
			pugi::xml_node capabilitiesNode = defNode.child("capabilities");
			if (capabilitiesNode) {
				// Edible capability
				pugi::xml_node edibleNode = capabilitiesNode.child("edible");
				if (edibleNode) {
					EdibleCapability edible;
					edible.nutrition = edibleNode.attribute("nutrition").as_float(0.3F);
					edible.quality = parseCapabilityQuality(edibleNode.attribute("quality").as_string());
					edible.spoilable = edibleNode.attribute("spoilable").as_bool(false);
					def.capabilities.edible = edible;
				}

				// Drinkable capability
				pugi::xml_node drinkableNode = capabilitiesNode.child("drinkable");
				if (drinkableNode) {
					DrinkableCapability drinkable;
					drinkable.quality = parseCapabilityQuality(drinkableNode.attribute("quality").as_string());
					def.capabilities.drinkable = drinkable;
				}

				// Sleepable capability
				pugi::xml_node sleepableNode = capabilitiesNode.child("sleepable");
				if (sleepableNode) {
					SleepableCapability sleepable;
					sleepable.quality = parseCapabilityQuality(sleepableNode.attribute("quality").as_string());
					sleepable.recoveryMultiplier = sleepableNode.attribute("recoveryMultiplier").as_float(1.0F);
					def.capabilities.sleepable = sleepable;
				}

				// Toilet capability
				pugi::xml_node toiletNode = capabilitiesNode.child("toilet");
				if (toiletNode) {
					ToiletCapability toilet;
					toilet.hygieneBonus = toiletNode.attribute("hygieneBonus").as_bool(false);
					def.capabilities.toilet = toilet;
				}

				// Waste capability (marker for bio piles)
				pugi::xml_node wasteNode = capabilitiesNode.child("waste");
				if (wasteNode) {
					def.capabilities.waste = WasteCapability{};
				}

				// Carryable capability (ground items like stones)
				// In unified model, the entity's defName IS the item - no separate itemDefName needed
				pugi::xml_node carryableNode = capabilitiesNode.child("carryable");
				if (carryableNode) {
					CarryableCapability carryable;
					carryable.quantity = carryableNode.attribute("quantity").as_uint(1);
					def.capabilities.carryable = carryable;
				}

				// Harvestable capability (bushes, plants that yield resources)
				pugi::xml_node harvestableNode = capabilitiesNode.child("harvestable");
				if (harvestableNode) {
					std::string yieldName = harvestableNode.attribute("yield").as_string("");
					if (yieldName.empty()) {
						LOG_WARNING(
							Engine,
							"AssetDef '%s' has <harvestable> without valid 'yield' attribute; skipping capability",
							def.defName.c_str()
						);
					} else {
						HarvestableCapability harvestable;
						harvestable.yieldDefName = yieldName;
						harvestable.amountMin = harvestableNode.attribute("amountMin").as_uint(1);
						harvestable.amountMax = harvestableNode.attribute("amountMax").as_uint(3);

						// Validate amountMax >= amountMin (swap if reversed to avoid invalid distribution)
						if (harvestable.amountMax < harvestable.amountMin) {
							LOG_WARNING(
								Engine,
								"AssetDef '%s' harvestable: amountMax (%u) < amountMin (%u); swapping values",
								def.defName.c_str(),
								harvestable.amountMax,
								harvestable.amountMin
							);
							std::swap(harvestable.amountMin, harvestable.amountMax);
						}

						harvestable.durability = harvestableNode.attribute("durability").as_float(40.0F);
						harvestable.destructive = harvestableNode.attribute("destructive").as_bool(true);
						harvestable.regrowthTime = harvestableNode.attribute("regrowthTime").as_float(0.0F);
						harvestable.totalResourceMin = harvestableNode.attribute("totalResourceMin").as_uint(0);
						harvestable.totalResourceMax = harvestableNode.attribute("totalResourceMax").as_uint(0);
						harvestable.requiredToolType = harvestableNode.attribute("requiresTool").as_string("");

						// Validate totalResourceMax >= totalResourceMin
						if (harvestable.totalResourceMax < harvestable.totalResourceMin) {
							LOG_WARNING(
								Engine,
								"AssetDef '%s' harvestable: totalResourceMax (%u) < totalResourceMin (%u); swapping",
								def.defName.c_str(),
								harvestable.totalResourceMax,
								harvestable.totalResourceMin
							);
							std::swap(harvestable.totalResourceMin, harvestable.totalResourceMax);
						}

						// Validate mutually exclusive harvesting modes:
						// - Resource pool: totalResourceMin/Max > 0
						// - Destructive: destructive = true
						// - Regrowth: regrowthTime > 0
						// Combinations are ambiguous - resource pool takes priority at runtime
						const bool hasResourcePool = (harvestable.totalResourceMin > 0U) || (harvestable.totalResourceMax > 0U);
						const bool hasDestructiveOrRegrowth = harvestable.destructive || (harvestable.regrowthTime > 0.0F);
						if (hasResourcePool && hasDestructiveOrRegrowth) {
							LOG_WARNING(
								Engine,
								"AssetDef '%s' harvestable: totalResourceMin/Max specified with destructive=true "
								"or regrowthTime>0; these modes are mutually exclusive (resource pool takes priority)",
								def.defName.c_str()
							);
						}

						def.capabilities.harvestable = harvestable;
					}
				}

				// Craftable capability (crafting stations)
				pugi::xml_node craftableNode = capabilitiesNode.child("craftable");
				if (craftableNode) {
					def.capabilities.craftable = CraftableCapability{};
				}

				// Storage capability (containers)
				pugi::xml_node storageNode = capabilitiesNode.child("storage");
				if (storageNode) {
					StorageCapability storage;
					storage.maxCapacity = storageNode.child("capacity").text().as_uint(50);

					// Parse accepted categories
					pugi::xml_node acceptsNode = storageNode.child("acceptsCategories");
					if (acceptsNode) {
						for (pugi::xml_node catNode : acceptsNode.children("category")) {
							std::string catName = catNode.text().as_string();
							if (catName == "RawMaterial") {
								storage.acceptedCategories.push_back(ItemCategory::RawMaterial);
							} else if (catName == "Food") {
								storage.acceptedCategories.push_back(ItemCategory::Food);
							} else if (catName == "Tool") {
								storage.acceptedCategories.push_back(ItemCategory::Tool);
							} else if (catName == "Furniture") {
								storage.acceptedCategories.push_back(ItemCategory::Furniture);
							} else if (!catName.empty()) {
								LOG_WARNING(Engine, "Unknown storage category '%s' in %s", catName.c_str(), def.defName.c_str());
							}
						}
					}
					def.capabilities.storage = storage;
				}
			}

			// Collision shape - optional physical footprint for navmesh/collision
			pugi::xml_node collisionNode = defNode.child("collision");
			if (collisionNode) {
				pugi::xml_node rectNode = collisionNode.child("rect");
				pugi::xml_node polygonNode = collisionNode.child("polygon");

				if (rectNode) {
					const float minX = rectNode.attribute("minX").as_float(0.0F);
					const float minY = rectNode.attribute("minY").as_float(0.0F);
					const float maxX = rectNode.attribute("maxX").as_float(0.0F);
					const float maxY = rectNode.attribute("maxY").as_float(0.0F);
					if (maxX > minX && maxY > minY) {
						def.collision.type			   = CollisionShapeType::Rect;
						def.collision.offsetMeters	   = {(minX + maxX) * 0.5F, (minY + maxY) * 0.5F};
						def.collision.halfExtentsMeters = {(maxX - minX) * 0.5F, (maxY - minY) * 0.5F};
					} else {
						LOG_WARNING(Engine, "AssetDef '%s' <collision><rect> is degenerate (max <= min); ignoring", def.defName.c_str());
					}
				} else if (polygonNode) {
					for (pugi::xml_node ptNode : polygonNode.children("point")) {
						def.collision.pointsMeters.push_back({
							ptNode.attribute("x").as_float(0.0F),
							ptNode.attribute("y").as_float(0.0F),
						});
					}
					if (def.collision.pointsMeters.size() < 3) {
						LOG_WARNING(
							Engine,
							"AssetDef '%s' <collision><polygon> has fewer than 3 points (%zu); ignoring",
							def.defName.c_str(),
							def.collision.pointsMeters.size()
						);
						def.collision.pointsMeters.clear();
					} else {
						def.collision.type = CollisionShapeType::Polygon;
					}
				} else {
					LOG_WARNING(Engine, "AssetDef '%s' has <collision> with no valid <rect> or <polygon>; ignoring", def.defName.c_str());
				}
			}

			// Store base folder for relative path resolution
			def.baseFolder = baseFolder;

			// Store definition
			loadedNames.push_back(def.defName);
			definitions[def.defName] = std::move(def);
			loadedCount++;
		}

		// Eager collision capture: procedural generators can emit their collision
		// footprint via asset:setCollisionRect, which only surfaces when the script
		// runs. Run each such script once now (generateAsset does NOT tessellate, so
		// this is cheap) so nav/collision see the rect at load instead of racing the
		// first lazy render. XML <collision> always wins; skip capture when set. Ground tiles
		// never collide, and their generators are the heaviest scripts, so they are skipped too.
		for (const std::string& name : loadedNames) {
			auto it = definitions.find(name);
			if (it == definitions.end()) {
				continue;
			}
			AssetDefinition& def = it->second;
			if (def.collision.blocks() || def.assetType != AssetType::Procedural || def.role == AssetRole::Ground) {
				continue;
			}
			GeneratedAsset tmp;
			if (!generateAsset(name, 0, tmp)) {
				LOG_WARNING(Engine, "Eager collision capture: generateAsset failed for '%s'; leaving collision None", name.c_str());
				continue;
			}
			if (tmp.emittedCollision.has_value()) {
				def.collision = *tmp.emittedCollision;
			}
		}

		// SVG-metadata collision capture (simple assets). An SVG-backed asset may
		// author its collider in <metadata>; this wins over any XML <collision> for
		// simple assets (documented precedence: SVG metadata is the authoritative
		// home for SVG assets). Runs eagerly at load so nav, which reads collision
		// without forcing a render, sees the collider before the first lazy
		// getTemplate. loadCollisionFromSvgMetadata bails cheaply when there's no
		// metadata, so assets without a collider pay only a file open + parse. Ground tiles never collide.
		for (const std::string& name : loadedNames) {
			auto it = definitions.find(name);
			if (it == definitions.end()) {
				continue;
			}
			AssetDefinition& def = it->second;
			if (def.assetType != AssetType::Simple || def.svgPaths.empty() || def.role == AssetRole::Ground) {
				continue;
			}
			std::string resolvedSvgPath = def.resolvePath(def.svgPaths.front()).string();
			if (auto shape = loadCollisionFromSvgMetadata(resolvedSvgPath, def.worldHeight)) {
				def.collision = *shape;
			}
		}

		// Eager motion resolution (simple assets declaring a <motion> file): resolve + cache + log
		// now, so the animation system finds it ready and any parse/pivot problems surface at load.
		for (const std::string& name : loadedNames) {
			auto it = definitions.find(name);
			if (it != definitions.end() && !it->second.motionPath.empty()) {
				getMotion(name);
			}
		}

		LOG_DEBUG(Engine, "Loaded %d asset definitions from %s", loadedCount, xmlPath.c_str());
		return loadedCount > 0;
	}

	size_t AssetRegistry::loadDefinitionsFromFolder(const std::string& folderPath, const std::function<void(int)>& onProgress) {
		namespace fs = std::filesystem;

		// Record load-blocking failures in the report too: in async mode the splash
		// observes done==true and then reads getValidationReport() to gate, so an
		// early return must leave the report reflecting the failure, not stale state.
		if (!fs::exists(folderPath)) {
			LOG_ERROR(Engine, "Asset definitions folder not found: %s", folderPath.c_str());
			m_validationReport.issues.clear();
			m_validationReport.add(Severity::Error, "", "", "Asset definitions folder not found", folderPath);
			return 0;
		}

		if (!fs::is_directory(folderPath)) {
			LOG_ERROR(Engine, "Path is not a directory: %s", folderPath.c_str());
			m_validationReport.issues.clear();
			m_validationReport.add(Severity::Error, "", "", "Asset path is not a directory", folderPath);
			return 0;
		}

		size_t totalLoaded = 0;
		size_t filesProcessed = 0;

		// Recursively iterate through all files in the folder
		// Only load "primary" XML files that match the FolderName/FolderName.xml pattern
		try {
			for (const auto& entry : fs::recursive_directory_iterator(folderPath)) {
				if (!entry.is_regular_file()) {
					continue;
				}

				// Only process .xml files
				if (entry.path().extension() != ".xml") {
					continue;
				}

				// Check if this is a primary XML file (FolderName/FolderName.xml pattern)
				// This prevents loading helper XMLs or non-primary definition files
				std::string filename = entry.path().stem().string();
				std::string parentFolder = entry.path().parent_path().filename().string();
				if (filename != parentFolder) {
					LOG_DEBUG(
						Engine,
						"Skipping non-primary XML: %s (filename '%s' != parent '%s')",
						entry.path().string().c_str(),
						filename.c_str(),
						parentFolder.c_str()
					);
					continue;
				}

				filesProcessed++;
				size_t beforeCount = definitions.size();

				if (loadDefinitions(entry.path().string())) {
					size_t loaded = definitions.size() - beforeCount;
					totalLoaded += loaded;
					LOG_DEBUG(Engine, "Loaded %zu definitions from %s", loaded, entry.path().string().c_str());
					if (onProgress) {
						onProgress(static_cast<int>(definitions.size()));
					}
				}
			}
		} catch (const fs::filesystem_error& e) {
			LOG_ERROR(Engine, "Filesystem error scanning '%s': %s", folderPath.c_str(), e.what());
			m_validationReport.issues.clear();
			m_validationReport.add(Severity::Error, "", "", std::string("Filesystem error scanning assets: ") + e.what(), folderPath);
			return totalLoaded;
		}

		LOG_INFO(
			Engine, "Asset folder scan complete: %zu definitions from %zu XML files in %s", totalLoaded, filesProcessed, folderPath.c_str()
		);

		// Build group index from loaded definitions
		buildGroupIndex();

		// Build string interning index for memory-efficient storage
		buildDefNameIndex();

		// Validate on load so the game (at launch) and the Asset Manager share one
		// report. No GL; safe on the load worker thread.
		m_validationReport = AssetValidator::validate(folderPath, m_sharedScriptsPath);
		prewarmTreeVariants();
		LOG_INFO(
			Engine, "Asset validation: %d error(s), %d warning(s)", m_validationReport.errorCount(), m_validationReport.warningCount()
		);

		return totalLoaded;
	}

	void AssetRegistry::beginLoadAsync(const std::string& folderPath) {
		if (m_loadThread.joinable()) {
			m_loadThread.join();
		}
		m_loadProgress.defsLoaded.store(0);
		m_loadProgress.done.store(false);
		m_loadProgress.started.store(true, std::memory_order_release);

		m_loadThread = std::thread([this, folderPath]() {
			loadDefinitionsFromFolder(folderPath, [this](int loaded) { m_loadProgress.defsLoaded.store(loaded); });
			// Release so a reader that observes done (acquire) sees all the writes
			// the worker made to definitions, indices, and the validation report.
			m_loadProgress.done.store(true, std::memory_order_release);
		});
	}

	AssetRegistry::~AssetRegistry() {
		if (m_loadThread.joinable()) {
			m_loadThread.join();
		}
	}

	const AssetDefinition* AssetRegistry::getDefinition(const std::string& defName) const {
#ifdef DEVELOPMENT_BUILD
		if (m_countingNameLookups.load(std::memory_order_relaxed)) {
			m_nameLookups.fetch_add(1, std::memory_order_relaxed);
		}
#endif
		auto it = definitions.find(defName);
		if (it != definitions.end()) {
			return &it->second;
		}
		return nullptr;
	}

	const AssetDefinition* AssetRegistry::getDefinitionById(uint32_t id) const {
		return id < m_idToDefinition.size() ? m_idToDefinition[id] : nullptr;
	}

	namespace {
		/// "Flora_TreeOak age class 'mature' variant 3", or "Flora_TreePine variant 3" without age classes.
		std::string describeTemplate(const AssetDefinition& def, TreeTraits traits) {
			std::string out = def.defName;
			if (!def.ageClasses.empty()) {
				out += " age class '" + def.ageClasses[traits.ageClass].name + "'";
			}
			return out + " variant " + std::to_string(traits.variant);
		}

		/// Farthest vertex from the template origin, in meters.
		float meshReach(const renderer::TessellatedMesh& mesh) {
			float reachSq = 0.0F;
			for (const auto& v : mesh.vertices) {
				reachSq = std::max(reachSq, (v.x * v.x) + (v.y * v.y));
			}
			return std::sqrt(reachSq);
		}

		float meshHeight(const renderer::TessellatedMesh& mesh) {
			if (mesh.vertices.empty()) {
				return 0.0F;
			}
			float minY = mesh.vertices.front().y;
			float maxY = minY;
			for (const auto& v : mesh.vertices) {
				minY = std::min(minY, v.y);
				maxY = std::max(maxY, v.y);
			}
			return maxY - minY;
		}

		void raiseTo(std::atomic<float>& target, float value) {
			float current = target.load(std::memory_order_relaxed);
			while (value > current && !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
			}
		}
	} // namespace

	size_t AssetRegistry::TemplateKeyHash::operator()(const TemplateKey& key) const {
		const size_t traits = (static_cast<size_t>(key.traits.ageClass) << 16U) | key.traits.variant;
		return std::hash<std::string>{}(key.defName) ^ (traits * 0x9E3779B97F4A7C15ULL);
	}

	std::optional<TreeTraits> AssetRegistry::templateTraits(const AssetDefinition& def, TreeTraits traits) {
		if (def.assetType == AssetType::Simple || def.role == AssetRole::Groundcover) {
			return TreeTraits{};
		}
		if (def.ageClasses.empty()) {
			if (traits.ageClass != 0 || traits.variant >= std::max<uint32_t>(def.variantCount, 1U)) {
				return std::nullopt;
			}
			return traits;
		}
		if (traits.ageClass >= def.ageClasses.size() || traits.variant >= def.ageClasses[traits.ageClass].variants) {
			return std::nullopt;
		}
		return traits;
	}

	uint32_t AssetRegistry::generationSeed(const AssetDefinition& def, TreeTraits traits) {
		const bool singleVariant =
			def.ageClasses.empty() ? def.variantCount <= 1 : def.ageClasses.size() == 1 && def.ageClasses.front().variants <= 1;
		if (singleVariant) {
			return 0;
		}
		if (def.ageClasses.empty()) {
			return variantSeed(def.defName, traits.variant);
		}
		return variantSeed(def.defName + ":" + def.ageClasses[traits.ageClass].name, traits.variant);
	}

	TreeTraits AssetRegistry::matureTraits(const AssetDefinition& def) {
		for (size_t i = 0; i < def.ageClasses.size(); ++i) {
			if (def.ageClasses[i].name == "mature") {
				return {.ageClass = static_cast<uint8_t>(i), .variant = 0};
			}
		}
		return {};
	}

	AssetRegistry::BuiltTemplate AssetRegistry::buildTemplate(const AssetDefinition& def, TreeTraits traits) {
		BuiltTemplate			  out;
		renderer::TessellatedMesh mesh;

		if (def.assetType == AssetType::Simple) {
			if (def.svgPaths.empty()) {
				LOG_ERROR(Engine, "Simple asset %s has no svgPath", def.defName.c_str());
				return out;
			}

			// Resolve SVG path relative to asset folder
			const std::string& svgPath = def.svgPaths.front();
			std::string		   resolvedSvgPath = def.resolvePath(svgPath).string();
			LOG_DEBUG(Engine, "Resolved SVG path: %s -> %s", svgPath.c_str(), resolvedSvgPath.c_str());

			std::vector<renderer::LoadedSVGShape> shapes;
			if (!renderer::loadSVG(resolvedSvgPath, kSvgCurveTolerance, shapes)) {
				LOG_ERROR(Engine, "Failed to load SVG: %s (resolved from %s)", resolvedSvgPath.c_str(), svgPath.c_str());
				return out;
			}

			const SvgMeterFrame frame		= computeSvgMeterFrame(shapes, def.worldHeight);
			const float			scaleFactor = frame.valid ? frame.scaleFactor : 1.0F;
			LOG_INFO(Engine, "SVG '%s': worldHeight=%.2f, scaleFactor=%.4f", def.defName.c_str(), def.worldHeight, scaleFactor);

			// Tessellate each shape (baking solid/gradient per-vertex colors) in SVG space, then
			// normalize the whole mesh to worldHeight.
			for (const auto& shape : shapes) {
				renderer::appendShapeMesh(shape, mesh);
			}
			for (auto& v : mesh.vertices) {
				v.x *= scaleFactor;
				v.y *= scaleFactor;
			}

			if (mesh.vertices.empty()) {
				LOG_ERROR(Engine, "Failed to tessellate SVG asset: %s", def.defName.c_str());
				return out;
			}
		} else {
			GeneratedAsset asset;
			std::string scriptError;
			if (!generateAsset(def.defName, generationSeed(def, traits), asset, traits, &scriptError)) {
				out.error = "Failed to generate " + describeTemplate(def, traits) + (scriptError.empty() ? "" : ": " + scriptError);
				LOG_ERROR(Engine, "%s", out.error.c_str());
				return out;
			}

			TessellatedPaths paths;
			paths.tessellate(asset, kFullDetail);
			std::vector<renderer::TessellatedMesh> meshes;
			const bool							   drawn = paths.meshesAt(kFullDetail, meshes);
			if (meshes.size() > 1 || paths.oversizeVertices > 0) {
				out.error = describeTemplate(def, traits) + " tessellates to " + std::to_string(paths.vertices.size() + paths.oversizeVertices) +
							" vertices; a template mesh holds at most " + std::to_string(TessellatedPaths::kMaxMeshVertices);
				LOG_ERROR(Engine, "%s", out.error.c_str());
				return out;
			}
			if (!drawn) {
				out.error = "Failed to tessellate " + describeTemplate(def, traits) + ": no geometry";
				LOG_ERROR(Engine, "%s", out.error.c_str());
				return out;
			}
			mesh = std::move(meshes.front());
		}

		out.mesh = std::move(mesh);
		return out;
	}

	uint32_t AssetRegistry::insertTemplate(const AssetDefinition& def, TreeTraits traits, BuiltTemplate built) {
		const bool groundcover = def.role == AssetRole::Groundcover;
		const bool isMature	   = traits == templateTraits(def, matureTraits(def));
		const bool defTall =
			isMature ? routesTall(built.mesh.has_value() ? meshHeight(*built.mesh) : 0.0F, groundcover) : isTallDef(def.defName);
		const float reach = built.mesh.has_value() ? meshReach(*built.mesh) * def.placement.scale.max : 0.0F;

		uint32_t id = 0;
		{
			std::lock_guard<std::mutex> lock(templateCacheMutex);
			auto [it, inserted] = m_templateIds.try_emplace(TemplateKey{def.defName, traits}, 0U);
			if (!inserted) {
				return it->second;
			}
			m_templates.push_back(std::make_unique<TemplateEntry>(TemplateEntry{std::move(built.mesh), defTall}));
			id			= static_cast<uint32_t>(m_templates.size());
			it->second = id;
		}
		if (!groundcover) {
			raiseTo(m_maxReach, reach);
		}
		return id;
	}

	uint32_t AssetRegistry::templateId(const std::string& defName, TreeTraits traits) {
		const AssetDefinition* def = getDefinition(defName);
		if (def == nullptr) {
			LOG_ERROR(Engine, "Definition not found: %s", defName.c_str());
			return 0;
		}
		const std::optional<TreeTraits> key = templateTraits(*def, traits);
		if (!key.has_value()) {
			LOG_ERROR(
				Engine, "%s has no age class %u variant %u", defName.c_str(), static_cast<unsigned>(traits.ageClass), static_cast<unsigned>(traits.variant)
			);
			return 0;
		}
		{
			std::lock_guard<std::mutex> lock(templateCacheMutex);
			auto						it = m_templateIds.find(TemplateKey{defName, *key});
			if (it != m_templateIds.end()) {
				return it->second;
			}
		}
		return insertTemplate(*def, *key, buildTemplate(*def, *key));
	}

	const renderer::TessellatedMesh* AssetRegistry::getTemplate(uint32_t templateId) const {
		if (templateId == 0) {
			return nullptr;
		}
		std::lock_guard<std::mutex> lock(templateCacheMutex);
		if (templateId > m_templates.size()) {
			return nullptr;
		}
		const std::optional<renderer::TessellatedMesh>& mesh = m_templates[templateId - 1]->mesh;
		return mesh.has_value() ? &*mesh : nullptr;
	}

	const renderer::TessellatedMesh* AssetRegistry::getTemplate(const std::string& defName, TreeTraits traits) {
		return getTemplate(templateId(defName, traits));
	}

	bool AssetRegistry::isTallDef(const std::string& defName) {
		const AssetDefinition* def = getDefinition(defName);
		if (def == nullptr) {
			return false;
		}
		const uint32_t id = templateId(defName, matureTraits(*def));
		if (id == 0) {
			return false;
		}
		std::lock_guard<std::mutex> lock(templateCacheMutex);
		return m_templates[id - 1]->defTall;
	}

	void AssetRegistry::prewarmTreeVariants() {
		struct Job {
			const AssetDefinition* def = nullptr;
			TreeTraits			   traits;
			bool				   mature = false;
			BuiltTemplate		   built;
		};
		std::vector<Job> jobs;
		for (const auto& [name, def] : definitions) {
			if (def.ageClasses.empty() || def.assetType != AssetType::Procedural) {
				continue;
			}
			const TreeTraits mature = matureTraits(def);
			for (size_t a = 0; a < def.ageClasses.size(); ++a) {
				for (uint16_t v = 0; v < def.ageClasses[a].variants; ++v) {
					const TreeTraits traits{.ageClass = static_cast<uint8_t>(a), .variant = v};
					jobs.push_back({.def = &def, .traits = traits, .mature = traits == mature});
				}
			}
		}
		if (jobs.empty()) {
			return;
		}
		// The def's mature variant 0 first, so filing the rest finds its routing decided. Ordered by
		// name otherwise, so ids come out the same every load.
		std::sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) {
			if (a.def->defName != b.def->defName) {
				return a.def->defName < b.def->defName;
			}
			if (a.mature != b.mature) {
				return a.mature;
			}
			return a.traits.ageClass != b.traits.ageClass ? a.traits.ageClass < b.traits.ageClass : a.traits.variant < b.traits.variant;
		});

		const auto			 start = std::chrono::steady_clock::now();
		foundation::TaskPool pool;
		pool.parallelFor(0, jobs.size(), 1, [&](size_t first, size_t last) {
			for (size_t i = first; i < last; ++i) {
				jobs[i].built = buildTemplate(*jobs[i].def, jobs[i].traits);
			}
		});
		for (Job& job : jobs) {
			if (!job.built.error.empty()) {
				m_validationReport.add(Severity::Error, job.def->defName, "ageClasses", job.built.error, "");
			}
			insertTemplate(*job.def, job.traits, std::move(job.built));
		}
		LOG_INFO(
			Engine,
			"Prewarmed %zu tree variants in %.1f ms",
			jobs.size(),
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()
		);
	}

	const AssetSilhouette* AssetRegistry::getSilhouette(const std::string& defName, TreeTraits traits) {
		return getSilhouette(templateId(defName, traits));
	}

	const AssetSilhouette* AssetRegistry::getSilhouette(uint32_t templateId) {
		{
			std::lock_guard<std::mutex> lk(m_silhouetteCacheMutex);
			auto						it = m_silhouetteCache.find(templateId);
			if (it != m_silhouetteCache.end()) {
				return &it->second;
			}
		}

		// Compute outside the silhouette lock (rasterization can be heavy), mirroring
		// getMotion. getTemplate guards itself with a separate mutex.
		constexpr std::int64_t kSilhouetteResolution	 = 256; // pixels along the longest side
		constexpr std::int64_t kHitRegionCloseRadiusPx = 4;	// bridge disjoint-blob gaps (whole-clump)

		AssetSilhouette sil;

		const renderer::TessellatedMesh* tmpl = getTemplate(templateId);
		if (tmpl != nullptr && !tmpl->vertices.empty() && !tmpl->indices.empty()) {
			// Bounds centre of the whole drawn mesh in the template frame == the dynamic render
			// path's -centerOffset, so the shared instance transform, outline, and hit-test never drift.
			const Foundation::Rect b = renderer::computeBounds(*tmpl);
			sil.boundsCenterMeters	 = {b.x + b.width * 0.5F, b.y + b.height * 0.5F};

			// Rasterize the ACTUAL rendered geometry: expand the tessellated triangles into
			// the template-local mm frame. This captures fills AND stroke bands (e.g. a reed's
			// stroke stem, which a fill-contour silhouette would miss), so the outline hugs
			// exactly what is drawn and the reed stays one connected shape.
			const bool hasColors = tmpl->hasColors();
			const auto faint	 = [&](uint16_t idx) { return hasColors && tmpl->colors[idx].a < kSilhouetteMinAlpha; };
			std::vector<geometry::Vec2i64> triVerts;
			triVerts.reserve(tmpl->indices.size());
			for (size_t t = 0; t + 2 < tmpl->indices.size(); t += 3) {
				const uint16_t i0 = tmpl->indices[t];
				const uint16_t i1 = tmpl->indices[t + 1];
				const uint16_t i2 = tmpl->indices[t + 2];
				if (faint(i0) && faint(i1) && faint(i2)) {
					continue;
				}
				for (const uint16_t idx : {i0, i1, i2}) {
					const Foundation::Vec2& v = tmpl->vertices[idx];
					triVerts.push_back(geometry::quantize({v.x, v.y}));
				}
			}
			if (!triVerts.empty()) {
				sil.rings	  = geometry::silhouetteOfTriangles(triVerts, kSilhouetteResolution, 0);
				sil.hitRegion = geometry::silhouetteOfTriangles(triVerts, kSilhouetteResolution, kHitRegionCloseRadiusPx);
			}
		}

		if (sil.hitRegion.empty()) {
			sil.hitRegion = sil.rings; // single-blob / degenerate: the outline is the hit region
		}
		sil.valid = !sil.rings.empty();

		std::lock_guard<std::mutex> lk(m_silhouetteCacheMutex);
		auto [it, inserted] = m_silhouetteCache.emplace(templateId, std::move(sil));
		return &it->second;
	}

	const MotionDef* AssetRegistry::getMotion(const std::string& defName) {
		{
			std::lock_guard<std::mutex> lk(m_motionCacheMutex);
			auto					   it = m_motionCache.find(defName);
			if (it != m_motionCache.end()) {
				return it->second.empty() ? nullptr : &it->second;
			}
		}

		MotionDef			   resolved; // stays empty unless resolution fully succeeds
		const AssetDefinition* def = getDefinition(defName);
		if (def != nullptr && !def->motionPath.empty() && def->assetType == AssetType::Simple && !def->svgPaths.empty()) {
			// Resolve the motion file path (@shared/<...> or relative to the asset folder).
			std::filesystem::path motionFile;
			const std::string&	  mp = def->motionPath;
			const std::string	  sharedPrefix = "@shared/";
			if (mp.rfind(sharedPrefix, 0) == 0 && !m_sharedScriptsPath.empty()) {
				motionFile = m_sharedScriptsPath.parent_path() / mp.substr(sharedPrefix.size());
			} else {
				motionFile = def->resolvePath(mp);
			}

			MotionDef parsed;
			if (!parseMotionXml(motionFile.string(), parsed)) {
				LOG_WARNING(Engine, "[Motion] %s: failed to parse %s", defName.c_str(), motionFile.string().c_str());
			} else {
				// Load the SVG for the meter frame (scaleFactor) and the authored node coordinates.
				const std::string					  svgFile = def->resolvePath(def->svgPaths.front()).string();
				std::vector<renderer::LoadedSVGShape> shapes;
				pugi::xml_document					  svgDoc;
				const bool							  svgOk =
					renderer::loadSVG(svgFile, kSvgCurveTolerance, shapes) && svgDoc.load_file(svgFile.c_str());
				const SvgMeterFrame frame = svgOk ? computeSvgMeterFrame(shapes, def->worldHeight) : SvgMeterFrame{};
				if (!svgOk || !frame.valid) {
					LOG_WARNING(Engine, "[Motion] %s: SVG load/frame failed; motion unresolved", defName.c_str());
				} else {
					const float							 sf = frame.scaleFactor;
					const pugi::xml_node				 svgRoot = svgDoc.child("svg");
					std::unordered_map<std::string, std::vector<glm::vec2>> partNodes;
					auto nodesFor = [&](const std::string& part) -> const std::vector<glm::vec2>& {
						auto found = partNodes.find(part);
						if (found == partNodes.end()) {
							const std::string d = findPartFirstPathD(svgRoot, part);
							found = partNodes.emplace(part, parseSvgPathNodes(d)).first;
						}
						return found->second;
					};

					int count = 0;
					for (auto& clip : parsed.clips) {
						for (auto& drv : clip.drivers) {
							const auto& nodes = nodesFor(drv.part);
							if (drv.node >= 0 && static_cast<size_t>(drv.node) < nodes.size()) {
								drv.pivot	 = nodes[static_cast<size_t>(drv.node)] * sf; // scaled, uncentered (= template space)
								drv.hasPivot = true;
							} else {
								LOG_WARNING(
									Engine,
									"[Motion] %s: part '%s' node %d out of range (%zu nodes)",
									defName.c_str(),
									drv.part.c_str(),
									drv.node,
									nodes.size()
								);
							}
							// Bring amplitudes into runtime units: rotation -> radians, pos -> meters.
							constexpr float kDegToRad = 3.14159265358979323846F / 180.0F;
							if (drv.channel == MotionChannel::Rotation) {
								drv.amp *= kDegToRad;
							} else if (drv.channel == MotionChannel::PosX || drv.channel == MotionChannel::PosY) {
								drv.amp *= sf;
							}
							++count;
							LOG_DEBUG(
								Engine,
								"[Motion] %s '%s' part=%s node=%d pivot=(%.4f,%.4f) amp=%.4f freq=%.2f phase=%.2f",
								defName.c_str(),
								clip.name.c_str(),
								drv.part.c_str(),
								drv.node,
								drv.pivot.x,
								drv.pivot.y,
								drv.amp,
								drv.freq,
								drv.phaseOffset
							);
						}
					}
					resolved = std::move(parsed);
					LOG_INFO(Engine, "[Motion] %s resolved %d driver(s), scaleFactor=%.5f", defName.c_str(), count, sf);
				}
			}
		}

		std::lock_guard<std::mutex> lk(m_motionCacheMutex);
		auto						emplaced = m_motionCache.emplace(defName, std::move(resolved));
		return emplaced.first->second.empty() ? nullptr : &emplaced.first->second;
	}

	std::filesystem::path AssetRegistry::resolveScriptPath(const AssetDefinition& def) const {
		const std::string kSharedPrefix = "@shared/";
		if (def.scriptPath.compare(0, kSharedPrefix.size(), kSharedPrefix) != 0) {
			return def.resolvePath(def.scriptPath);
		}
		if (m_sharedScriptsPath.empty()) {
			LOG_ERROR(Engine, "Shared scripts path not configured, but @shared/ prefix used in: %s", def.scriptPath.c_str());
			return {};
		}
		return m_sharedScriptsPath / def.scriptPath.substr(kSharedPrefix.size());
	}

	std::filesystem::path AssetRegistry::preludeDir() const {
		return m_sharedScriptsPath.empty() ? std::filesystem::path{} : m_sharedScriptsPath / "lib";
	}

	bool AssetRegistry::generateAsset(
		const std::string& defName, uint32_t seed, GeneratedAsset& outAsset, TreeTraits traits, std::string* error
	) {
		const AssetDefinition* def = getDefinition(defName);
		if (def == nullptr) {
			LOG_ERROR(Engine, "Definition not found: %s", defName.c_str());
			return false;
		}

		if (def->assetType != AssetType::Procedural) {
			LOG_ERROR(Engine, "Asset %s is not procedural", defName.c_str());
			return false;
		}
		if (!def->ageClasses.empty() && traits.ageClass >= def->ageClasses.size()) {
			LOG_ERROR(Engine, "Asset %s has no age class %u", defName.c_str(), static_cast<unsigned>(traits.ageClass));
			return false;
		}

		// Set up generation context
		GenerationContext ctx;
		ctx.seed = seed;
		ctx.variantIndex = traits.variant;
		if (!def->ageClasses.empty()) {
			ctx.ageClass = def->ageClasses[traits.ageClass].name;
		}
		if (def->role == AssetRole::Ground) {
			ctx.periodMeters = def->ground.periodMeters;
		}
		outAsset.clear();

		// Check if this is a Lua script generator
		if (def->isLuaGenerator()) {
			try {
				const std::string resolvedScriptPath = resolveScriptPath(*def).string();
				if (resolvedScriptPath.empty()) {
					return false;
				}
				LOG_DEBUG(Engine, "Resolved script: %s -> %s", def->scriptPath.c_str(), resolvedScriptPath.c_str());

				LuaGenerator luaGen(resolvedScriptPath, preludeDir());
				const bool generated = luaGen.generate(ctx, def->params, outAsset);
				if (!generated && error != nullptr) {
					*error = luaGen.lastError();
				}
				return generated;
			} catch (const std::exception& e) {
				LOG_ERROR(Engine, "LuaGenerator error for '%s': %s", def->scriptPath.c_str(), e.what());
				if (error != nullptr) {
					*error = e.what();
				}
				return false;
			}
		}

		// Create C++ generator from registry
		auto generator = GeneratorRegistry::Get().create(def->generatorName.c_str());
		if (!generator) {
			LOG_ERROR(Engine, "Generator not found: %s", def->generatorName.c_str());
			return false;
		}

		return generator->generate(ctx, def->params, outAsset);
	}

	bool AssetRegistry::buildPaths(const std::string& defName, uint32_t seed, TessellatedPaths& outPaths, float maxPixelsPerMeter) {
		outPaths = {};
		const AssetDefinition* def = getDefinition(defName);
		if (def == nullptr) {
			LOG_ERROR(Engine, "buildPaths: definition not found: %s", defName.c_str());
			return false;
		}
		const auto	   start = std::chrono::steady_clock::now();
		GeneratedAsset asset;
		const bool drawn = def->role == AssetRole::Ground && def->assetType == AssetType::Simple ? readGroundSvgVariant(*def, seed, asset)
																								 : generateAsset(defName, seed, asset);
		if (!drawn) {
			return false;
		}
		outPaths.generateMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

		if (def->role == AssetRole::Ground) {
			rankUnsetHeights(asset); // before the wrap, so a copy keeps its original's rank
			wrapGroundPaths(asset, def->ground.periodMeters);
		}
		outPaths.tessellate(asset, maxPixelsPerMeter);
		return true;
	}

	bool AssetRegistry::buildMeshes(
		const std::string& defName, uint32_t seed, std::vector<renderer::TessellatedMesh>& outMeshes, float pixelsPerMeter
	) {
		outMeshes.clear();

		const AssetDefinition* def = getDefinition(defName);
		if (def == nullptr) {
			LOG_ERROR(Engine, "buildMeshes: definition not found: %s", defName.c_str());
			return false;
		}

		// A simple asset other than a ground tile is one drawing; reuse the cached, normalized template.
		if (def->assetType == AssetType::Simple && def->role != AssetRole::Ground) {
			const renderer::TessellatedMesh* tmpl = getTemplate(defName, TreeTraits{});
			if (tmpl == nullptr || tmpl->vertices.empty()) {
				return false;
			}
			outMeshes.push_back(*tmpl);
			return true;
		}

		// Procedural assets and ground tiles: this seed's form fresh (uncached), tessellated.
		TessellatedPaths paths;
		return buildPaths(defName, seed, paths, pixelsPerMeter) && paths.meshesAt(pixelsPerMeter, outMeshes);
	}

	bool AssetRegistry::buildMesh(const std::string& defName, uint32_t seed, renderer::TessellatedMesh& outMesh) {
		outMesh.clear();

		std::vector<renderer::TessellatedMesh> meshes;
		if (!buildMeshes(defName, seed, meshes)) {
			return false;
		}
		return takeSingleMesh(defName, meshes, outMesh);
	}

	void AssetRegistry::clear() {
		// Finish any in-flight async load before tearing down the data it writes.
		if (m_loadThread.joinable()) {
			m_loadThread.join();
		}
		definitions.clear();
		clearCaches();
		groupIndex.clear();
		// Reset the string-interning index too (mirrors clearDefinitions); otherwise name<->id
		// and capability lookups return stale mappings for now-destroyed defs until a reload.
		m_defNameToId.clear();
		m_idToDefName.clear();
		m_idToDefinition.clear();
		m_capabilityMasks.clear();
		m_edibleItemNames.clear();
		m_edibleItemIds.clear();
		m_harvestablesByYieldId.clear();
		m_harvestYieldIds.clear();
		m_validationReport.issues.clear();
		m_loadProgress.started.store(false);
		m_loadProgress.done.store(false);
		m_loadProgress.defsLoaded.store(0);
	}

	std::vector<std::string> AssetRegistry::getDefinitionNames() const {
		std::vector<std::string> names;
		names.reserve(definitions.size());
		for (const auto& [name, _] : definitions) {
			names.push_back(name);
		}
		return names;
	}

	// ============================================================================
	// Group Index Implementation
	// ============================================================================

	void AssetRegistry::buildGroupIndex() {
		groupIndex.clear();

		for (const auto& [defName, def] : definitions) {
			for (const auto& group : def.placement.groups) {
				groupIndex[group].push_back(defName);
			}
		}

		if (!groupIndex.empty()) {
			LOG_DEBUG(Engine, "Built group index: %zu groups", groupIndex.size());
		}
	}

	std::vector<std::string> AssetRegistry::getGroupMembers(const std::string& groupName) const {
		auto it = groupIndex.find(groupName);
		if (it != groupIndex.end()) {
			return it->second;
		}
		return {};
	}

	std::vector<std::string> AssetRegistry::getGroups() const {
		std::vector<std::string> groups;
		groups.reserve(groupIndex.size());
		for (const auto& [name, _] : groupIndex) {
			groups.push_back(name);
		}
		return groups;
	}

	bool AssetRegistry::hasGroup(const std::string& groupName) const {
		return groupIndex.find(groupName) != groupIndex.end();
	}

	// ============================================================================
	// String Interning Implementation
	// ============================================================================

	void AssetRegistry::buildDefNameIndex() {
		m_defNameToId.clear();
		m_idToDefName.clear();
		m_idToDefinition.clear();
		m_capabilityMasks.clear();
		m_edibleItemNames.clear();
		m_edibleItemIds.clear();
		m_harvestablesByYieldId.clear();
		m_harvestYieldIds.clear();

		// Reserve ID 0 as "invalid"
		m_idToDefName.push_back("");
		m_idToDefinition.push_back(nullptr);
		m_capabilityMasks.push_back(0);

		// Build ID mapping for all definitions
		uint32_t nextId = 1;
		for (const auto& [defName, def] : definitions) {
			m_defNameToId[defName] = nextId;
			m_idToDefName.push_back(defName);
			m_idToDefinition.push_back(&def);
			m_capabilityMasks.push_back(computeCapabilityMask(def));
			if (def.isEdible()) {
				m_edibleItemNames.push_back(defName);
				m_edibleItemIds.push_back(nextId);
			}
			nextId++;
		}
		std::sort(m_edibleItemNames.begin(), m_edibleItemNames.end());
		std::sort(m_edibleItemIds.begin(), m_edibleItemIds.end());
		// Filed once every definition has its id, so a yield's id is its definition's.
		for (const auto& [defName, def] : definitions) {
			indexHarvestYield(def, m_defNameToId[defName]);
		}

		LOG_DEBUG(Engine, "Built defName index: %zu entries", m_idToDefName.size() - 1);
	}

	void AssetRegistry::indexHarvestYield(const AssetDefinition& def, uint32_t id) {
		if (id < m_harvestYieldIds.size()) {
			m_harvestYieldIds[id] = 0;
		}
		if (!def.capabilities.harvestable.has_value() || def.capabilities.harvestable->yieldDefName.empty()) {
			return;
		}
		const uint32_t yieldId = internName(def.capabilities.harvestable->yieldDefName);
		if (id >= m_harvestYieldIds.size()) {
			m_harvestYieldIds.resize(id + 1, 0);
		}
		m_harvestYieldIds[id] = yieldId;
		if (yieldId >= m_harvestablesByYieldId.size()) {
			m_harvestablesByYieldId.resize(yieldId + 1);
		}
		auto&	   ids = m_harvestablesByYieldId[yieldId];
		const auto at = std::lower_bound(ids.begin(), ids.end(), id);
		if (at == ids.end() || *at != id) {
			ids.insert(at, id);
		}
	}

	uint32_t AssetRegistry::internName(const std::string& defName) {
		if (const auto it = m_defNameToId.find(defName); it != m_defNameToId.end()) {
			return it->second;
		}
		const auto id = static_cast<uint32_t>(m_idToDefName.size());
		m_defNameToId[defName] = id;
		m_idToDefName.push_back(defName);
		m_idToDefinition.push_back(nullptr);
		m_capabilityMasks.push_back(0);
		return id;
	}

	const std::vector<uint32_t>& AssetRegistry::harvestablesYielding(uint32_t yieldDefNameId) const {
		if (yieldDefNameId < m_harvestablesByYieldId.size()) {
			return m_harvestablesByYieldId[yieldDefNameId];
		}
		static const std::vector<uint32_t> kNone;
		return kNone;
	}

	uint32_t AssetRegistry::getDefNameId(const std::string& defName) const {
#ifdef DEVELOPMENT_BUILD
		if (m_countingNameLookups.load(std::memory_order_relaxed)) {
			m_nameLookups.fetch_add(1, std::memory_order_relaxed);
		}
#endif
		auto it = m_defNameToId.find(defName);
		if (it != m_defNameToId.end()) {
			return it->second;
		}
		return 0; // Invalid ID
	}

	const std::string& AssetRegistry::getDefName(uint32_t id) const {
		if (id < m_idToDefName.size()) {
			return m_idToDefName[id];
		}
		static const std::string kEmptyString;
		return kEmptyString;
	}

	uint16_t AssetRegistry::getCapabilityMask(uint32_t id) const {
		if (id < m_capabilityMasks.size()) {
			return m_capabilityMasks[id];
		}
		return 0;
	}

	bool AssetRegistry::hasCapability(uint32_t id, CapabilityType capability) const {
		uint16_t mask = getCapabilityMask(id);
		return (mask & static_cast<uint16_t>(1U << static_cast<uint8_t>(capability))) != 0;
	}

	float AssetRegistry::getItemMassKg(const std::string& defName) const {
		const auto* def = getDefinition(defName);
		if (def != nullptr && def->itemProperties.has_value()) {
			return def->itemProperties->massKg;
		}
		return 1.0F;
	}

	float AssetRegistry::itemMassKg(uint32_t id) const {
		const auto* def = getDefinitionById(id);
		return def != nullptr && def->itemProperties.has_value() ? def->itemProperties->massKg : 1.0F;
	}

	uint32_t AssetRegistry::harvestYieldId(uint32_t id) const {
		return id < m_harvestYieldIds.size() ? m_harvestYieldIds[id] : 0U;
	}

	bool AssetRegistry::yieldsEdible(uint32_t id) const {
		const auto* yield = getDefinitionById(harvestYieldId(id));
		return yield != nullptr && yield->isEdible();
	}

	const std::string& AssetRegistry::getToolType(uint32_t id) const {
		const auto* def = getDefinitionById(id);
		if (def != nullptr) {
			return def->toolType;
		}
		static const std::string kEmptyString;
		return kEmptyString;
	}

	uint32_t AssetRegistry::registerSyntheticDefinition(const std::string& defName, uint16_t capabilityMask) {
		// Check if already registered
		auto it = m_defNameToId.find(defName);
		if (it != m_defNameToId.end()) {
			// A name first interned as an undefined harvest yield has an id but no definition and no
			// capabilities; the synthetic definition gives it its capabilities. A real definition keeps its own.
			if (m_idToDefinition[it->second] == nullptr) {
				m_capabilityMasks[it->second] = capabilityMask;
			}
			return it->second;
		}

		// Assign new ID
		auto newId = static_cast<uint32_t>(m_idToDefName.size());
		m_defNameToId[defName] = newId;
		m_idToDefName.push_back(defName);
		m_idToDefinition.push_back(nullptr);
		m_capabilityMasks.push_back(capabilityMask);

		LOG_DEBUG(Engine, "Registered synthetic definition '%s' with ID %u, capabilities 0x%04X", defName.c_str(), newId, capabilityMask);

		return newId;
	}

	// ============================================================================
	// Testing API
	// ============================================================================

	void AssetRegistry::registerTestDefinition(AssetDefinition def) {
		std::string defName = def.defName;
		const uint16_t mask = computeCapabilityMask(def);
		const bool edible = def.isEdible();
		if (const auto previous = definitions.find(defName);
			previous != definitions.end() && previous->second.capabilities.harvestable.has_value()) {
			const uint32_t previousYieldId = getDefNameId(previous->second.capabilities.harvestable->yieldDefName);
			if (previousYieldId < m_harvestablesByYieldId.size()) {
				std::erase(m_harvestablesByYieldId[previousYieldId], getDefNameId(defName));
			}
		}
		definitions[defName] = std::move(def);

		const auto edibleAt = std::lower_bound(m_edibleItemNames.begin(), m_edibleItemNames.end(), defName);
		const bool listed = edibleAt != m_edibleItemNames.end() && *edibleAt == defName;
		if (edible && !listed) {
			m_edibleItemNames.insert(edibleAt, defName);
		} else if (!edible && listed) {
			m_edibleItemNames.erase(edibleAt);
		}

		// Assign the interning id by APPENDING, never by rebuilding the whole index. A full rebuild
		// iterates the unordered definitions map, so every prior id could be reshuffled when the map
		// rehashes -- making a defNameId captured between two registrations silently come to name a
		// different definition (it stayed put on MSVC, reordered on libstdc++, surfacing as a CI-only
		// AIDecisionSystem failure). Appending keeps already-issued ids stable across registrations.
		if (m_idToDefName.empty()) {
			// First registration: seat the reserved invalid id 0 (mirrors buildDefNameIndex).
			m_idToDefName.push_back("");
			m_idToDefinition.push_back(nullptr);
			m_capabilityMasks.push_back(0);
		}
		const AssetDefinition& stored = definitions[defName];
		auto				   existing = m_defNameToId.find(defName);
		if (existing != m_defNameToId.end()) {
			// Re-registration of the same name updates its mask in place; id is unchanged.
			m_capabilityMasks[existing->second] = mask;
			m_idToDefinition[existing->second] = &stored;
		} else {
			const auto newId = static_cast<uint32_t>(m_idToDefName.size());
			m_defNameToId[defName] = newId;
			m_idToDefName.push_back(defName);
			m_idToDefinition.push_back(&stored);
			m_capabilityMasks.push_back(mask);
		}
		const uint32_t id = m_defNameToId[defName];
		indexHarvestYield(stored, id);
		const auto edibleIdAt = std::lower_bound(m_edibleItemIds.begin(), m_edibleItemIds.end(), id);
		const bool idListed = edibleIdAt != m_edibleItemIds.end() && *edibleIdAt == id;
		if (edible && !idListed) {
			m_edibleItemIds.insert(edibleIdAt, id);
		} else if (!edible && idListed) {
			m_edibleItemIds.erase(edibleIdAt);
		}

		LOG_DEBUG(Engine, "Registered test definition: %s", defName.c_str());
	}

	void AssetRegistry::clearCaches() {
		{
			std::lock_guard<std::mutex> lk(templateCacheMutex);
			m_templateIds.clear();
			m_templates.clear();
		}
		m_maxReach.store(0.0F, std::memory_order_relaxed);
		{
			std::lock_guard<std::mutex> lk(m_motionCacheMutex);
			m_motionCache.clear();
		}
		{
			std::lock_guard<std::mutex> lk(m_silhouetteCacheMutex);
			m_silhouetteCache.clear();
		}
	}

	void AssetRegistry::clearDefinitions() {
		definitions.clear();
		clearCaches();
		groupIndex.clear();
		m_defNameToId.clear();
		m_idToDefName.clear();
		m_idToDefinition.clear();
		m_capabilityMasks.clear();
		m_edibleItemNames.clear();
		m_edibleItemIds.clear();
		m_harvestablesByYieldId.clear();
		m_harvestYieldIds.clear();
		LOG_DEBUG(Engine, "Cleared all definitions");
	}

} // namespace assets
