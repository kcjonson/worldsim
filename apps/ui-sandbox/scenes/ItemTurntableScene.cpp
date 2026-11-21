// item-turntable: a parts asset's item views (docs/technical/rendering/rotatable-placed-items.md, sections 2 to 7) on
// a plain floor. The demo workbench in assets/sandbox/items, loaded through the game's loader and validation, drawn
// large at a turn the viewer sets, beside a sheet of the same item every 15 degrees. Each view is built by
// assets::buildItemMesh, uploaded once, and drawn strip by strip in its stored order by index range.
//
// Keys: Left and Right turn by the drawing tools' angle step (snapping.xml), Alt+Left and Alt+Right by 1 degree;
// 1, 2, 3, and 4 toggle the collider, the use areas, the clearance, and the depth strips with their anchors.
// Dev server: /api/control?action=turn&degrees=35 sets the turn, /api/control?action=overlay&collider=1&use=0&
// clearance=0&strips=1 sets the overlays, and /api/state?what=scene reads them back with the view and its validation.

#include "SceneTypes.h"

#include <assets/AssetBootstrap.h>
#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <assets/AssetValidator.h>
#include <assets/ConstructionRegistry.h>
#include <assets/DetailLevels.h>
#include <assets/ItemView.h>
#include <assets/PartsAsset.h>
#include <assets/PointTurn.h>
#include <assets/ViewConfig.h>
#include <theme/Keys.h>
#include <theme/Theme.h>

#include <application/App.h>
#include <graphics/Color.h>
#include <graphics/Rect.h>
#include <input/InputTypes.h>
#include <primitives/InstanceData.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <ui/shapes/Text.h>
#include <ui/theme/Tokens.h>
#include <utils/Log.h>
#include <utils/ResourcePath.h>

#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

	constexpr const char* kSceneName = "item-turntable";
	constexpr const char* kItemsFolder = "assets/sandbox/items";
	constexpr const char* kSnappingFile = "assets/config/construction/snapping.xml";
	constexpr const char* kDefName = "Workbench";

	constexpr uint32_t kQuantaPerDegree = 100;
	constexpr uint32_t kSheetStepDegrees = 15;
	constexpr size_t   kSheetCount = 360 / kSheetStepDegrees;
	constexpr size_t   kSheetColumns = 6;
	constexpr size_t   kSheetRows = kSheetCount / kSheetColumns;

	// ui px
	constexpr float kMargin = 24.0F;
	constexpr float kHeaderHeight = 84.0F;
	constexpr float kCellGap = 8.0F;
	constexpr float kLabelHeight = 22.0F;
	constexpr float kLargeShare = 0.42F;
	// Room round the farthest thing drawn, so nothing touches a cell's edge.
	constexpr float kFitMargin = 1.08F;
	// Logical px
	constexpr float kOverlayWidth = 2.0F;
	constexpr float kAnchorRadius = 4.0F;

	const Foundation::Color kFloor(0.557F, 0.541F, 0.514F, 1.0F); // #8E8A83
	const Foundation::Color kColliderColor(0.85F, 0.12F, 0.12F, 1.0F);
	const Foundation::Color kUseAreaColor(0.15F, 0.75F, 0.25F, 1.0F);
	const Foundation::Color kClearanceColor(0.95F, 0.75F, 0.1F, 1.0F);
	const Foundation::Color kStripColor(0.1F, 0.75F, 0.95F, 1.0F);
	const Foundation::Color kAnchorColor(0.9F, 0.2F, 0.85F, 1.0F);

	struct Overlays {
		bool collider = false;
		bool useAreas = false;
		bool clearance = false;
		bool strips = false;
	};

	// One built view and its uploaded mesh.
	struct DrawnView {
		assets::ItemView			  view;
		Renderer::InstancedMeshHandle handle;
		bool						  built = false;
	};

	// Where everything goes, in ui px.
	struct SceneLayout {
		Foundation::Rect						  largeBox;
		Foundation::Vec2						  largeCenter{0.0F, 0.0F};
		float									  largePixelsPerMeter = 0.0F;
		std::array<Foundation::Rect, kSheetCount> cellBoxes{};
		std::array<Foundation::Vec2, kSheetCount> cellCenters{};
		float									  sheetPixelsPerMeter = 0.0F;
	};

	std::string degreesText(uint32_t turnQuanta) {
		return std::format("{:.2f}", static_cast<double>(turnQuanta) / kQuantaPerDegree);
	}

	std::string jsonEscaped(const std::string& text) {
		std::string out;
		for (const char character : text) {
			if (character == '"' || character == '\\') {
				out += '\\';
			}
			out += (static_cast<unsigned char>(character) < 0x20) ? ' ' : character;
		}
		return out;
	}

	/// A flag the request sets with key=0 or key=1: false when it is written any other way.
	bool readFlag(const app::DevRequest& request, const char* key, bool& flag) {
		const auto found = request.params.find(key);
		if (found == request.params.end()) {
			return true;
		}
		if (found->second != "0" && found->second != "1") {
			return false;
		}
		flag = found->second == "1";
		return true;
	}

	class ItemTurntableScene : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& owner) override {
			LOG_INFO(UI, "Item turntable - parts asset views at any turn");
			loadDemoAsset();
			buildInterface();

			devRegistrations.push_back(owner.devServer().control("turn", [this](const app::DevRequest& request) {
				const auto	 degrees = request.params.find("degrees");
				char*		 end = nullptr;
				const double value = degrees == request.params.end() ? 0.0 : std::strtod(degrees->second.c_str(), &end);
				if (degrees == request.params.end() || end == degrees->second.c_str() || *end != '\0' || !std::isfinite(value)) {
					return app::DevResponse{.status = 400, .body = R"({"error":"turn needs degrees=<number>"})"};
				}
				turnQuanta = assets::turnQuantaFromDegrees(value);
				return app::DevResponse{.body = exportState()};
			}));
			devRegistrations.push_back(owner.devServer().control("overlay", [this](const app::DevRequest& request) {
				Overlays requested = overlays;
				if (!readFlag(request, "collider", requested.collider) || !readFlag(request, "use", requested.useAreas) ||
					!readFlag(request, "clearance", requested.clearance) || !readFlag(request, "strips", requested.strips)) {
					return app::DevResponse{
						.status = 400, .body = R"({"error":"overlay flags are collider, use, clearance, and strips, each 0 or 1"})"
					};
				}
				overlays = requested;
				return app::DevResponse{.body = exportState()};
			}));
		}

		void onExit() override {
			devRegistrations.clear();
			large = {};
			sheet = {};
			definition = nullptr;
			// The registry is one for the app: give the other scenes back the game's assets this scene replaced.
			assets::initializeAssetSystem(false);
		}

		void update(const app::FrameContext& frame) override {
			readKeys(frame.input);
			layout = computeLayout();
			placeLabels();
			rebuildViews();
			refreshText();
		}

		void render(float /*alpha*/) override {
			const float scale = ui().getScale();
			Renderer::Primitives::drawRect({.bounds = logical(layout.largeBox, scale), .style = {.fill = kFloor}, .id = "turntable_floor"});
			for (size_t index = 0; index < kSheetCount; ++index) {
				// The label sits on the background above its floor.
				Foundation::Rect floor = layout.cellBoxes[index];
				floor.y += kLabelHeight;
				floor.height -= kLabelHeight;
				Renderer::Primitives::drawRect({.bounds = logical(floor, scale), .style = {.fill = kFloor}, .id = "sheet_floor"});
			}
			// The views draw at once, not batched: the floor has to be down first.
			Renderer::Primitives::flush();

			drawView(large, layout.largeCenter * scale, layout.largePixelsPerMeter * scale);
			for (size_t index = 0; index < kSheetCount; ++index) {
				drawView(sheet[index], layout.cellCenters[index] * scale, layout.sheetPixelsPerMeter * scale);
			}
			drawOverlays(layout.largeCenter * scale, layout.largePixelsPerMeter * scale);
		}

		std::string exportState() const override {
			const assets::ValidationReport& report = assets::AssetRegistry::Get().getValidationReport();
			std::string						issues;
			for (const assets::ValidationIssue& issue : report.issues) {
				issues += std::string(issues.empty() ? "" : ",") + "\"" + jsonEscaped(issue.defName + ": " + issue.message) + "\"";
			}
			std::string strips;
			for (const assets::ViewStrip& strip : large.view.strips) {
				strips += std::format(
					"{}{{\"firstIndex\":{},\"indexCount\":{},\"anchorLocalY\":{:.4f},\"flat\":{}}}",
					strips.empty() ? "" : ",",
					strip.firstIndex,
					strip.indexCount,
					strip.anchorLocalY,
					strip.flat
				);
			}
			return std::format(
				R"({{"def":"{}","loaded":{},"turnDegrees":{},"turnQuanta":{},"stepDegrees":{},"level":{},"vertices":{},"strips":[{}],)"
				R"("viewError":"{}","overlays":{{"collider":{},"use":{},"clearance":{},"strips":{}}},)"
				R"("validation":{{"errors":{},"warnings":{},"issues":[{}]}}}})",
				kDefName,
				definition != nullptr,
				degreesText(turnQuanta),
				turnQuanta,
				degreesText(stepQuanta),
				large.view.level,
				large.view.vertexCount,
				strips,
				jsonEscaped(large.view.error),
				overlays.collider,
				overlays.useAreas,
				overlays.clearance,
				overlays.strips,
				report.errorCount(),
				report.warningCount(),
				issues
			);
		}

	  private:
		void loadDemoAsset() {
			const std::string folder = Foundation::findResourceString(kItemsFolder);
			if (folder.empty()) {
				LOG_ERROR(UI, "Item turntable: %s not found", kItemsFolder);
				return;
			}
			// The game's loader and validation, over the sandbox's items alone.
			assets::loadAssetSystem({.definitions = folder, .sharedScripts = {}, .recipes = {}}, false);
			const assets::ValidationReport& report = assets::AssetRegistry::Get().getValidationReport();
			for (const assets::ValidationIssue& issue : report.issues) {
				LOG_WARNING(UI, "Item turntable: %s: %s (%s)", issue.defName.c_str(), issue.message.c_str(), issue.context.c_str());
			}
			if (!assets::GameViewConfig::Get().loaded()) {
				LOG_ERROR(UI, "Item turntable: the view config didn't load");
				return;
			}
			viewConfig = assets::GameViewConfig::Get().config();

			assets::ConstructionRegistry& construction = assets::ConstructionRegistry::Get();
			if (!construction.loadSnapping(Foundation::findResourceString(kSnappingFile))) {
				LOG_WARNING(UI, "Item turntable: %s didn't load; stepping by its default", kSnappingFile);
			}
			stepQuanta = assets::turnQuantaFromDegrees(construction.snapping().angleIncrementDegrees);

			definition = assets::AssetRegistry::Get().getDefinition(kDefName);
			if (definition == nullptr || definition->parts == nullptr) {
				LOG_ERROR(UI, "Item turntable: no parts def %s in %s", kDefName, folder.c_str());
				definition = nullptr;
				return;
			}
			fitRadiusMeters = assets::itemReachMeters(*definition->parts, viewConfig);
			auto reach = [this](const std::vector<assets::AreaPolygon>& polygons) {
				for (const assets::AreaPolygon& polygon : polygons) {
					for (const glm::vec2& point : polygon.outer) {
						fitRadiusMeters = std::max(fitRadiusMeters, std::hypot(point.x, point.y));
					}
				}
			};
			for (const assets::ItemArea& area : definition->parts->useAreas) {
				reach(area.polygons);
			}
			reach(definition->parts->clearance.polygons);
			fitRadiusMeters *= kFitMargin;
		}

		void buildInterface() {
			theme::apply(ui());
			title = root().addChild<ui::Text>({.position = {kMargin, kMargin}, .style = ui::styles::text::title, .text = "Workbench"});
			root().addChild<ui::Text>(
				{.position = {kMargin, kMargin + 30.0F},
				 .style = ui::styles::text::secondary,
				 .text =
					 "Left / Right: turn a step    Alt + Left / Right: 1 degree    1 collider    2 use areas    3 clearance    4 strips"}
			);
			status = root().addChild<ui::Text>({.position = {kMargin, kMargin + 52.0F}, .style = ui::styles::text::secondary, .text = ""});
			for (size_t index = 0; index < kSheetCount; ++index) {
				sheetLabels[index] =
					root().addChild<ui::Text>({.style = ui::styles::label, .text = std::to_string(index * kSheetStepDegrees) + " deg"});
			}
		}

		void readKeys(const foundation::InputSnapshot& input) {
			using foundation::Key;
			const bool	   fine = theme::key(input, Key::LeftAlt).down || theme::key(input, Key::RightAlt).down;
			const uint32_t step = (fine ? kQuantaPerDegree : stepQuanta) % assets::kTurnQuantaPerTurn;
			if (theme::key(input, Key::Right).pressed()) {
				turnQuanta = (turnQuanta + step) % assets::kTurnQuantaPerTurn;
			}
			if (theme::key(input, Key::Left).pressed()) {
				turnQuanta = (turnQuanta + assets::kTurnQuantaPerTurn - step) % assets::kTurnQuantaPerTurn;
			}
			overlays.collider ^= theme::key(input, Key::Num1).pressed();
			overlays.useAreas ^= theme::key(input, Key::Num2).pressed();
			overlays.clearance ^= theme::key(input, Key::Num3).pressed();
			overlays.strips ^= theme::key(input, Key::Num4).pressed();
		}

		SceneLayout computeLayout() {
			const Foundation::Vec2 canvas = ui().getCanvas();
			const float			   contentTop = kMargin + kHeaderHeight;
			const float			   contentHeight = std::max(1.0F, canvas.y - contentTop - kMargin);
			const float			   largeWidth = std::max(1.0F, (canvas.x - 3.0F * kMargin) * kLargeShare);
			const float			   sheetLeft = 2.0F * kMargin + largeWidth;
			const float			   sheetWidth = std::max(1.0F, canvas.x - kMargin - sheetLeft);
			const float			   diameter = 2.0F * std::max(fitRadiusMeters, 0.01F);

			SceneLayout result;
			result.largeBox = {kMargin, contentTop, largeWidth, contentHeight};
			result.largeCenter = {kMargin + largeWidth * 0.5F, contentTop + contentHeight * 0.5F};
			result.largePixelsPerMeter = std::min(largeWidth, contentHeight) / diameter;

			const float cellWidth = (sheetWidth - static_cast<float>(kSheetColumns - 1) * kCellGap) / static_cast<float>(kSheetColumns);
			const float cellHeight = (contentHeight - static_cast<float>(kSheetRows - 1) * kCellGap) / static_cast<float>(kSheetRows);
			result.sheetPixelsPerMeter = std::max(0.0F, std::min(cellWidth, cellHeight - kLabelHeight)) / diameter;
			for (size_t index = 0; index < kSheetCount; ++index) {
				const float left = sheetLeft + static_cast<float>(index % kSheetColumns) * (cellWidth + kCellGap);
				const float top = contentTop + static_cast<float>(index / kSheetColumns) * (cellHeight + kCellGap);
				result.cellBoxes[index] = {left, top, cellWidth, cellHeight};
				result.cellCenters[index] = {left + cellWidth * 0.5F, top + kLabelHeight + (cellHeight - kLabelHeight) * 0.5F};
			}
			return result;
		}

		void placeLabels() {
			for (size_t index = 0; index < kSheetCount; ++index) {
				sheetLabels[index].get()->setPosition({layout.cellBoxes[index].x, layout.cellBoxes[index].y});
			}
		}

		uint8_t levelFor(float uiPixelsPerMeter) {
			return assets::detailLevelFor(std::span<const float>(definition->detailLevels), uiPixelsPerMeter * ui().getScale());
		}

		void rebuild(DrawnView& drawn, uint32_t quanta, uint8_t level) {
			if (drawn.built && drawn.view.turnQuanta == quanta && drawn.view.level == level) {
				return;
			}
			drawn.view = assets::buildItemMesh(*definition, quanta, level, viewConfig);
			drawn.handle = {};
			if (!drawn.view.error.empty()) {
				LOG_ERROR(UI, "Item turntable: no view at %s degrees: %s", degreesText(quanta).c_str(), drawn.view.error.c_str());
			} else if (!drawn.view.mesh.vertices.empty()) {
				drawn.handle = Renderer::Primitives::uploadInstancedMesh(drawn.view.mesh, 1);
			}
			drawn.built = true;
		}

		void rebuildViews() {
			if (definition == nullptr) {
				return;
			}
			rebuild(large, turnQuanta, levelFor(layout.largePixelsPerMeter));
			const uint8_t sheetLevel = levelFor(layout.sheetPixelsPerMeter);
			for (size_t index = 0; index < kSheetCount; ++index) {
				rebuild(sheet[index], static_cast<uint32_t>(index * kSheetStepDegrees * kQuantaPerDegree), sheetLevel);
			}
		}

		void refreshText() {
			const assets::ValidationReport& report = assets::AssetRegistry::Get().getValidationReport();
			title.get()->setText(std::format("Workbench at {} degrees", degreesText(turnQuanta)));
			std::string line = std::format(
				"Step {} degrees    level {}    {} vertices    {} strips    validation: {} errors, {} warnings",
				degreesText(stepQuanta),
				large.view.level,
				large.view.vertexCount,
				large.view.strips.size(),
				report.errorCount(),
				report.warningCount()
			);
			if (definition == nullptr) {
				line = "The demo workbench didn't load: see the log";
			} else if (!large.view.error.empty()) {
				line = "No view: " + large.view.error;
			}
			status.get()->setText(line);
		}

		static Foundation::Rect logical(const Foundation::Rect& box, float scale) {
			return {box.x * scale, box.y * scale, box.width * scale, box.height * scale};
		}

		void drawView(const DrawnView& drawn, Foundation::Vec2 center, float pixelsPerMeter) const {
			if (!drawn.handle.isValid() || pixelsPerMeter <= 0.0F) {
				return;
			}
			int viewportWidth = 0;
			int viewportHeight = 0;
			Renderer::Primitives::getLogicalViewport(viewportWidth, viewportHeight);
			// The item stands at the world origin; the camera puts the origin on `center`.
			const Foundation::Vec2 halfViewport(static_cast<float>(viewportWidth) * 0.5F, static_cast<float>(viewportHeight) * 0.5F);
			const Renderer::Primitives::WorldCamera camera{
				.position = (halfViewport - center) / pixelsPerMeter, .zoom = 1.0F, .pixelsPerMeter = pixelsPerMeter
			};
			const Renderer::InstanceData instance;
			for (const assets::ViewStrip& strip : drawn.view.strips) {
				Renderer::Primitives::drawInstanced(
					drawn.handle, &instance, 1, camera, {.first = strip.firstIndex, .count = strip.indexCount}
				);
			}
		}

		void drawRing(
			std::span<const glm::vec2> ring,
			float					   radians,
			Foundation::Vec2		   center,
			float					   pixelsPerMeter,
			const Foundation::Color&   color
		) const {
			std::vector<Foundation::Vec2> points;
			points.reserve(ring.size());
			for (const glm::vec2& point : ring) {
				const glm::vec2 onScreen = assets::turnedPoint(point, radians) * pixelsPerMeter;
				points.emplace_back(center.x + onScreen.x, center.y + onScreen.y);
			}
			Renderer::Primitives::drawPolyline(
				{.points = points, .width = kOverlayWidth, .color = color, .closed = true, .id = "turntable_overlay"}
			);
		}

		void drawArea(
			const assets::ItemArea&	 area,
			float					 radians,
			Foundation::Vec2		 center,
			float					 pixelsPerMeter,
			const Foundation::Color& color
		) const {
			for (const assets::AreaPolygon& polygon : area.polygons) {
				drawRing(polygon.outer, radians, center, pixelsPerMeter, color);
				for (const std::vector<glm::vec2>& hole : polygon.holes) {
					drawRing(hole, radians, center, pixelsPerMeter, color);
				}
			}
		}

		void drawOverlays(Foundation::Vec2 center, float pixelsPerMeter) const {
			if (definition == nullptr) {
				return;
			}
			const float radians = assets::turnRadiansFromQuanta(turnQuanta);
			if (overlays.collider) {
				for (const assets::CollisionShape& ring : definition->partsColliders) {
					drawRing(ring.pointsMeters, radians, center, pixelsPerMeter, kColliderColor);
				}
			}
			if (overlays.useAreas) {
				for (const assets::ItemArea& area : definition->parts->useAreas) {
					drawArea(area, radians, center, pixelsPerMeter, kUseAreaColor);
				}
			}
			if (overlays.clearance) {
				drawArea(definition->parts->clearance, radians, center, pixelsPerMeter, kClearanceColor);
			}
			if (overlays.strips) {
				// Strip boxes and anchors are already on the screen, relative to the pivot: no turn.
				for (const assets::ViewStrip& strip : large.view.strips) {
					const std::array<glm::vec2, 4> box{
						glm::vec2{strip.minX, strip.minY},
						glm::vec2{strip.maxX, strip.minY},
						glm::vec2{strip.maxX, strip.maxY},
						glm::vec2{strip.minX, strip.maxY}
					};
					drawRing(box, 0.0F, center, pixelsPerMeter, kStripColor);
					// The flat strip's anchor is the lowest float, which has nowhere on the screen to go.
					if (strip.flat) {
						continue;
					}
					const float anchorY = center.y + strip.anchorLocalY * pixelsPerMeter;
					const float anchorLeft = center.x + std::max(strip.minX, -fitRadiusMeters) * pixelsPerMeter;
					const float anchorRight = center.x + std::min(strip.maxX, fitRadiusMeters) * pixelsPerMeter;
					Renderer::Primitives::drawLine(
						{.start = {anchorLeft, anchorY},
						 .end = {anchorRight, anchorY},
						 .style = {.color = kAnchorColor, .width = kOverlayWidth},
						 .id = "turntable_anchor"}
					);
					Renderer::Primitives::drawCircle(
						{.center = {(anchorLeft + anchorRight) * 0.5F, anchorY},
						 .radius = kAnchorRadius,
						 .style = {.fill = kAnchorColor},
						 .id = "turntable_anchor"}
					);
				}
			}
		}

		const assets::AssetDefinition*	   definition = nullptr;
		assets::ViewConfig				   viewConfig;
		float							   fitRadiusMeters = 1.0F;
		uint32_t						   turnQuanta = 0;
		uint32_t						   stepQuanta = kSheetStepDegrees * kQuantaPerDegree;
		Overlays						   overlays;
		SceneLayout						   layout;
		DrawnView						   large;
		std::array<DrawnView, kSheetCount> sheet;

		ui::Ref<ui::Text>						   title;
		ui::Ref<ui::Text>						   status;
		std::array<ui::Ref<ui::Text>, kSheetCount> sheetLabels;

		std::vector<app::DevServer::Registration> devRegistrations;
	};

} // namespace

namespace ui_sandbox::scenes {
	extern const ui_sandbox::SceneInfo ItemTurntable = {kSceneName, []() { return std::make_unique<ItemTurntableScene>(); }};
} // namespace ui_sandbox::scenes
