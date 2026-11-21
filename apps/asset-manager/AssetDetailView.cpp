#include "AssetDetailView.h"
#include "GroundBakeView.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/ItemView.h>
#include <assets/PartsAsset.h>
#include <assets/TurnQuanta.h>
#include <assets/ViewConfig.h>
#include <input/InputTypes.h>
#include <theme/Tokens.h>
#include <ui/Context.h>
#include <ui/components/Badge.h>
#include <ui/components/Button.h>
#include <ui/components/Checkbox.h>
#include <ui/components/SegmentedControl.h>
#include <ui/components/TextInput.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>
#include <ui/theme/Tokens.h>
#include <world/rendering/ItemViewDrawing.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <set>
#include <sstream>

namespace asset_manager {

	namespace {

		constexpr float kPad = 16.0F;
		constexpr float kGap = 12.0F;
		constexpr float kInfoWidth = 360.0F;

		// A parts def's preview steps and its sheet are the placement tool's angle step.
		constexpr uint32_t kStepDegrees = 15;
		constexpr uint32_t kQuantaPerDegree = 100;
		constexpr uint32_t kSheetCount = 360 / kStepDegrees;
		constexpr uint32_t kSheetColumns = 12;

		std::string degreesText(uint32_t turnQuanta) {
			return std::format("{:.2f}", static_cast<double>(turnQuanta) / kQuantaPerDegree);
		}

		std::string counted(size_t count, const char* noun) {
			return std::format("{} {}{}", count, noun, count == 1 ? "" : "s");
		}

		std::string meters(float value) {
			return std::format("{:g}", std::round(static_cast<double>(value) * 1000.0) / 1000.0);
		}

		/// Where an area's shape came from, as written, with its sizes in metres.
		std::string shapeSource(const assets::AuthoredAreaShape& shape) {
			using Kind = assets::AuthoredAreaShape::Kind;
			const std::string span =
				std::format("{}, {} to {}, {} m", meters(shape.minX), meters(shape.minY), meters(shape.maxX), meters(shape.maxY));
			switch (shape.kind) {
				case Kind::Around:
					return std::format("around, the collider grown {} m", meters(shape.distanceMeters));
				case Kind::Margin:
					return std::format("margin {} m", meters(shape.distanceMeters));
				case Kind::Circle:
					return std::format("circle at {}, {} m, radius {} m", meters(shape.x), meters(shape.y), meters(shape.radius));
				case Kind::Rect:
					return "rect " + span;
				case Kind::Drawing:
					return "drawing " + shape.file + " over " + span;
			}
			return {};
		}

		ui::Layout row(float gap) {
			return {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Row, .gap = gap, .alignItems = ui::AlignItems::Center, .flexShrink = 0.0F};
		}

		std::string readFile(const std::filesystem::path& path) {
			std::ifstream file(path, std::ios::binary);
			if (!file) {
				return "(could not read " + path.string() + ")";
			}
			std::ostringstream ss;
			ss << file.rdbuf();
			std::string content = ss.str();
			content.erase(std::remove(content.begin(), content.end(), '\r'), content.end());
			return content;
		}

		std::string fmtFloat(float v) {
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
			return buf;
		}

		std::string toUpper(std::string s) {
			for (auto& c : s) {
				c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			}
			return s;
		}

		ui::Layout flex(ui::FlexDirection direction, float gap, float grow = 0.0F) {
			return {.display = ui::Display::Flex, .flexDirection = direction, .gap = gap, .flexGrow = grow};
		}

		// A column shrinks its children down to nothing, text included, so what must keep its height
		// doesn't shrink, and the part that scrolls or stretches takes up the difference.
		const ui::Layout kKeepHeight{.flexShrink = 0.0F};
		const ui::Layout kHug{.flexShrink = 0.0F, .alignSelf = ui::AlignItems::Start};

	} // namespace

	AssetDetailView::AssetDetailView(const Args& args)
		: coordinateSystem(args.coordinateSystem) {}

	void AssetDetailView::onMount() {
		build();
	}

	void AssetDetailView::setAsset(const std::string& name) {
		const assets::AssetDefinition* def = assets::AssetRegistry::Get().getDefinition(name);
		if (name != defName) {
			partsLevel.reset(); // each asset opens at its own finest level
		}
		defName = def != nullptr ? name : std::string();
		procedural = def != nullptr && def->assetType == assets::AssetType::Procedural;
		parts = def != nullptr && def->assetType == assets::AssetType::Parts && def->parts != nullptr;
		ground = def != nullptr && def->role == assets::AssetRole::Ground;
		variantBase = procedural ? 1U : 0U; // an SVG ground def's seed is its variant, so it starts at the first SVG
		if (getContext() != nullptr) {
			build();
		}
	}

	void AssetDetailView::build() {
		if (content.get() != nullptr) {
			removeChild(content);
		}
		variants = {};
		tiling = {};
		largePreview = {};
		sheetPreviews.clear();
		turnEntry = {};
		content = addChild<ui::Component>({
			.size = {ui::percent(100.0F), ui::percent(100.0F)},
			.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = kGap, .padding = kPad},
		});
		const assets::AssetDefinition* def = assets::AssetRegistry::Get().getDefinition(defName);
		if (def == nullptr) {
			content.addChild<ui::Text>(
				{.layout = kKeepHeight, .style = ui::styles::text::secondary, .text = "Select an asset from the tree"}
			);
			return;
		}

		const std::string category = def->baseFolder.empty() ? "asset" : def->baseFolder.parent_path().filename().string();
		content.addChild<ui::Text>({.layout = kKeepHeight, .style = ui::styles::label, .text = toUpper(category)});
		content.addChild<ui::Text>({.layout = kKeepHeight, .style = ui::styles::text::title, .text = defName});
		content.addChild<ui::Badge>({
			.layout = kHug,
			.label = parts ? "PARTS" : procedural ? "PROCEDURAL" : "SIMPLE",
			.tone = parts ? ui::Tone::Default : procedural ? ui::Tone::Accent : ui::Tone::Info,
		});

		if (parts) {
			ui::Ref<ui::Component> columns = content.addChild<ui::Component>({.minSize = {ui::kAuto, 0.0F}, .layout = flex(ui::FlexDirection::Row, 20.0F, 1.0F)});
			partsPreview(columns.addChild<ui::Component>({.layout = flex(ui::FlexDirection::Column, kGap, 1.0F)}));
			partsInfo(columns.addChild<ui::Component>({
				.id = "am_parts_info",
				.size = {kInfoWidth, ui::kAuto},
				// Room on the right for the scrollbar, so wrapped text never runs under it.
				.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = kGap, .padding = {0.0F, kGap, 0.0F, 0.0F}},
				.overflow = ui::Overflow::Scroll,
			}));
			return;
		}

		if (ground) {
			content.addChild<ui::SegmentedControl>({
				.id = "am_ground_mode",
				.layout = kHug,
				.options = {"Vectors", "Bake"},
				.selected = bakeMode ? 1 : 0,
				.controlSize = ui::ControlSize::Sm,
				.onChange =
					[this](int index) {
						bakeMode = index == 1;
						build();
					},
			});
			if (bakeMode) {
				content.addChild<GroundBakeView>({
					.id = "am_ground_bake",
					.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = kGap, .flexGrow = 1.0F},
					.overflow = ui::Overflow::Scroll,
					.defName = defName,
					.coordinateSystem = coordinateSystem,
				});
				return;
			}
		}

		ui::Ref<ui::Component> columns = content.addChild<ui::Component>({.layout = flex(ui::FlexDirection::Row, 20.0F, 1.0F)});
		preview(columns.addChild<ui::Component>({.layout = flex(ui::FlexDirection::Column, kGap, 1.0F)}));
		info(columns.addChild<ui::Component>({.size = {kInfoWidth, ui::kAuto}, .layout = flex(ui::FlexDirection::Column, kGap)}));
	}

	// One framed thumbnail, or six in a 2x3 grid under Randomize; the collider over the first. A ground
	// tile, procedural or SVG, puts the first variant, tiled 3x3, to the left of the grid.
	void AssetDetailView::preview(ui::Ref<ui::Component> column) {
		const auto cell = [this](ui::Ref<ui::Component> parent, int index) {
			ui::Ref<ui::Rectangle> frame =
				parent.addChild<ui::Rectangle>({.layout = {.padding = 8.0F, .flexGrow = 1.0F}, .style = ui::styles::inset});
			variants[static_cast<std::size_t>(index)] = frame.addChild<AssetThumbnail>({
				.size = {ui::percent(100.0F), ui::percent(100.0F)},
				.animated = true,
				.collider = index == 0 && !ground,
				.coordinateSystem = coordinateSystem,
			});
		};
		if (!procedural && !ground) {
			cell(column, 0);
		} else {
			column.addChild<ui::Button>({.layout = kHug, .label = "Randomize", .controlSize = ui::ControlSize::Sm, .onPress = [this] {
											 randomize();
										 }});
			ui::Ref<ui::Component> grid = column;
			if (ground) {
				ui::Ref<ui::Component> row = column.addChild<ui::Component>({.layout = flex(ui::FlexDirection::Row, kGap, 1.0F)});
				ui::Ref<ui::Rectangle> frame =
					row.addChild<ui::Rectangle>({.layout = {.padding = 8.0F, .flexGrow = 2.0F}, .style = ui::styles::inset});
				tiling = frame.addChild<AssetThumbnail>(
					{.size = {ui::percent(100.0F), ui::percent(100.0F)}, .tiles = 3, .coordinateSystem = coordinateSystem}
				);
				grid = row.addChild<ui::Component>({.layout = flex(ui::FlexDirection::Column, kGap, 1.0F)});
			}
			for (int row = 0; row < 3; ++row) {
				ui::Ref<ui::Component> pair = grid.addChild<ui::Component>({.layout = flex(ui::FlexDirection::Row, kGap, 1.0F)});
				cell(pair, row * 2);
				cell(pair, (row * 2) + 1);
			}
		}
		applyVariantSeeds();
	}

	// The asset's facts, its validation issues, and its source XML.
	void AssetDetailView::info(ui::Ref<ui::Component> column) {
		const assets::AssetDefinition&		   def = *assets::AssetRegistry::Get().getDefinition(defName);
		const ui::Theme&					   palette = getContext()->getTheme();

		std::string meta;
		if (!def.label.empty()) {
			meta += def.label + "\n";
		}
		meta += "worldHeight  " + fmtFloat(def.worldHeight);
		for (const std::string& svgPath : def.svgPaths) {
			meta += "\nsvg  " + svgPath;
		}
		if (!def.scriptPath.empty()) {
			meta += "\nscript  " + def.scriptPath;
		}
		if (ground) {
			meta += "\nground  " + fmtFloat(def.ground.periodMeters) + " m at " + std::to_string(def.ground.pixelsPerMeter) + " px/m, " +
					std::to_string(def.variantCount) + " variants";
		}
		if (!def.placement.groups.empty()) {
			std::string groups;
			for (size_t i = 0; i < def.placement.groups.size(); ++i) {
				groups += (i == 0 ? "" : ", ") + def.placement.groups[i];
			}
			meta += "\ngroups  " + groups;
		}
		column.addChild<ui::Text>({.layout = kKeepHeight, .style = ui::styles::text::secondary, .text = meta, .wrap = true});

		std::string warnings;
		bool		anyError = false;
		for (const auto& issue : assets::AssetRegistry::Get().getValidationReport().issues) {
			if (issue.defName != defName) {
				continue;
			}
			const bool isError = issue.severity == assets::Severity::Error;
			anyError = anyError || isError;
			warnings += (warnings.empty() ? "" : "\n") + std::string(isError ? "[error] " : "[warning] ");
			if (!issue.field.empty()) {
				warnings += issue.field + ": ";
			}
			warnings += issue.message;
		}
		if (!warnings.empty()) {
			const ui::ColorToken tone = anyError ? theme::tokens::theme::color::statusCrit : theme::tokens::theme::color::statusWarn;
			column.addChild<ui::Text>(
				{.layout = kKeepHeight,
				 .style = ui::styles::text::body.extend({.color = palette.get(tone)}),
				 .text = warnings,
				 .wrap = true}
			);
		}
		definitionWell(column, true);
	}

	// The def's source XML in a well that scrolls, filling the column's height, or at its full height in a
	// column that scrolls itself.
	void AssetDetailView::definitionWell(ui::Ref<ui::Component> column, bool fill) {
		const assets::AssetDefinition&		  def = *assets::AssetRegistry::Get().getDefinition(defName);
		const std::filesystem::path xmlPath = def.baseFolder / (def.baseFolder.filename().string() + ".xml");
		column.addChild<ui::Text>(
			{.layout = kKeepHeight, .style = ui::styles::label, .text = toUpper("Definition  -  " + xmlPath.filename().string())}
		);
		ui::Ref<ui::Rectangle> well = column.addChild<ui::Rectangle>({
			.layout =
				{.display = ui::Display::Flex,
				 .flexDirection = ui::FlexDirection::Column,
				 .padding = kGap,
				 .flexGrow = fill ? 1.0F : 0.0F,
				 .flexShrink = fill ? 1.0F : 0.0F},
			.style = ui::styles::inset,
			.overflow = ui::Overflow::Scroll,
		});
		// Unwrapped, as the file reads: a long path has nowhere to break, so the well scrolls sideways.
		well.addChild<ui::Text>(
			{.layout = {.alignSelf = ui::AlignItems::Start}, .style = ui::styles::text::mono, .text = readFile(xmlPath)}
		);
	}

	// The item view at the pane's turn, its controls and overlay toggles above it, the sheet of turns below.
	void AssetDetailView::partsPreview(ui::Ref<ui::Component> column) {
		const assets::AssetDefinition&	def = *assets::AssetRegistry::Get().getDefinition(defName);
		const assets::ItemOverlayStyle& look = assets::ConstructionRegistry::Get().rendering().itemOverlays;

		ui::Ref<ui::Component> controls = column.addChild<ui::Component>({.layout = row(kGap)});
		controls.addChild<ui::Button>({
			.id = "am_turn_back",
			.label = std::format("-{}", kStepDegrees),
			.controlSize = ui::ControlSize::Sm,
			.onPress = [this] { setPartsTurn((partsTurnQuanta + assets::kTurnQuantaPerTurn - kStepDegrees * kQuantaPerDegree) % assets::kTurnQuantaPerTurn); },
		});
		turnEntry = controls.addChild<ui::TextInput>({
			.id = "am_turn",
			.size = {88.0F, ui::kAuto},
			.text = degreesText(partsTurnQuanta),
			.onSubmit =
				[this](const std::string& text) {
					// A number and nothing else; any size, wrapped into a turn before it becomes quanta.
					double			   degrees = 0.0;
					const char*		   end = text.data() + text.size();
					const auto		   parsed = std::from_chars(text.data(), end, degrees);
					const bool		   whole = parsed.ec == std::errc() && parsed.ptr == end && std::isfinite(degrees);
					setPartsTurn(whole ? assets::turnQuantaFromDegrees(std::fmod(degrees, 360.0)) : partsTurnQuanta);
				},
		});
		controls.addChild<ui::Button>({
			.id = "am_turn_forward",
			.label = std::format("+{}", kStepDegrees),
			.controlSize = ui::ControlSize::Sm,
			.onPress = [this] { setPartsTurn((partsTurnQuanta + kStepDegrees * kQuantaPerDegree) % assets::kTurnQuantaPerTurn); },
		});
		controls.addChild<ui::Text>({.style = ui::styles::text::secondary, .text = "degrees    click the view, then Left / Right turn it, Alt 1 degree"});

		// The detail level, the finest unless one is picked; the counts follow it, so a pick rebuilds the pane.
		std::vector<std::string> levels;
		for (const float density : def.detailLevels) {
			levels.push_back(std::format("{:g} px/m", density));
		}
		if (levels.size() > 1) {
			column.addChild<ui::SegmentedControl>({
				.id = "am_level",
				.layout = kHug,
				.options = levels,
				.selected = partsPreviewLevel(def, partsLevel),
				.controlSize = ui::ControlSize::Sm,
				.onChange =
					[this](int index) {
						partsLevel = static_cast<uint8_t>(index);
						pendingBuild = true;
						requestUpdate();
					},
			});
		}

		// The toggles are the legend: each overlay's swatch beside its name.
		ui::Ref<ui::Component> legend = column.addChild<ui::Component>({.id = "am_overlays", .layout = row(16.0F)});
		const auto			   toggle = [&](const char* id, const char* label, const assets::StyleColor& color, bool PartsOverlays::*flag) {
			  ui::Ref<ui::Component> entry = legend.addChild<ui::Component>({.layout = row(6.0F)});
			  entry.addChild<ui::Rectangle>({.size = {12.0F, 12.0F}, .layout = kKeepHeight, .style = {.backgroundColor = worldview::colorOf(color)}});
			  entry.addChild<ui::Checkbox>({
				  .id = id,
				  .label = label,
				  .checked = partsOverlays.*flag,
				  .onChange =
					  [this, flag](bool checked) {
						  partsOverlays.*flag = checked;
						  applyOverlays();
					  },
			  });
		};
		toggle("am_overlay_use", "Use areas", look.useAreaColor, &PartsOverlays::useAreas);
		toggle("am_overlay_clearance", "Clearance", look.clearanceColor, &PartsOverlays::clearance);
		toggle("am_overlay_footprints", "Footprints", look.footprintColor, &PartsOverlays::footprints);
		toggle("am_overlay_collider", "Collider", look.colliderColor, &PartsOverlays::collider);
		toggle("am_overlay_strips", "Depth strips", look.stripColor, &PartsOverlays::strips);

		// Focusable, so a click on it takes Left and Right; the arrows move focus as usual everywhere else.
		ui::Ref<ui::Rectangle> frame = column.addChild<ui::Rectangle>({
			.id = "am_parts_large",
			.minSize = {ui::kAuto, 0.0F},
			.layout = {.padding = 8.0F, .flexGrow = 3.0F},
			.style = ui::styles::inset,
			.focusable = true,
			.onKeyDown =
				[this](ui::KeyEvent& event) {
					if (event.consumed || event.type != ui::KeyEvent::Type::Down ||
						(event.key != foundation::Key::Left && event.key != foundation::Key::Right)) {
						return;
					}
					const uint32_t step = (event.mods & foundation::ModAlt) != 0 ? kQuantaPerDegree : kStepDegrees * kQuantaPerDegree;
					const uint32_t forward = event.key == foundation::Key::Right ? step : assets::kTurnQuantaPerTurn - step;
					setPartsTurn((partsTurnQuanta + forward) % assets::kTurnQuantaPerTurn);
					event.consume();
				},
		});
		largePreview = frame.addChild<PartsPreview>({
			.size = {ui::percent(100.0F), ui::percent(100.0F)},
			.defName = defName,
			.turnQuanta = partsTurnQuanta,
			.overlays = partsOverlays,
			.level = partsLevel,
		});

		ui::Ref<ui::Component> sheet = column.addChild<ui::Component>(
			{.id = "am_parts_sheet", .minSize = {ui::kAuto, 0.0F}, .layout = flex(ui::FlexDirection::Column, 6.0F, 2.0F)}
		);
		for (uint32_t sheetRow = 0; sheetRow < kSheetCount / kSheetColumns; ++sheetRow) {
			ui::Ref<ui::Component> cells =
				sheet.addChild<ui::Component>({.minSize = {ui::kAuto, 0.0F}, .layout = flex(ui::FlexDirection::Row, 6.0F, 1.0F)});
			for (uint32_t cellIndex = 0; cellIndex < kSheetColumns; ++cellIndex) {
				const uint32_t quanta = (sheetRow * kSheetColumns + cellIndex) * kStepDegrees * kQuantaPerDegree;
				ui::Ref<ui::Rectangle> cell = cells.addChild<ui::Rectangle>({
					.id = std::format("am_sheet_{}", quanta / kQuantaPerDegree),
					// A token width that the row's grow shares out evenly, whatever each label's width.
					.size = {1.0F, ui::kAuto},
					.minSize = {0.0F, 0.0F},
					.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .padding = 4.0F, .flexGrow = 1.0F},
					.style = ui::styles::inset,
					.pointerEvents = ui::PointerEvents::Unit,
					.onClick = [this, quanta](ui::PointerEvent&) { setPartsTurn(quanta); },
				});
				cell.addChild<ui::Text>({.layout = kKeepHeight, .style = ui::styles::label, .text = std::to_string(quanta / kQuantaPerDegree)});
				sheetPreviews.push_back(cell.addChild<PartsPreview>({
					.minSize = {0.0F, 0.0F},
					.layout = {.flexGrow = 1.0F},
					.defName = defName,
					.turnQuanta = quanta,
					.overlays = partsOverlays,
					.level = partsLevel,
					.labels = false,
				}));
			}
		}
	}

	// Counts, the use areas and clearance with each shape's source, the drawings with their proportion checks, and the
	// validation issues by part.
	void AssetDetailView::partsInfo(ui::Ref<ui::Component> column) {
		const assets::AssetDefinition& def = *assets::AssetRegistry::Get().getDefinition(defName);
		const assets::PartsAsset&	   item = *def.parts;
		const ui::Theme&			   palette = getContext()->getTheme();
		const auto toned = [&palette](ui::ColorToken token) { return ui::styles::text::body.extend({.color = palette.get(token)}); };
		const ui::Style ok = toned(theme::tokens::theme::color::statusOk);
		const ui::Style warn = toned(theme::tokens::theme::color::statusWarn);
		const ui::Style crit = toned(theme::tokens::theme::color::statusCrit);

		const auto keyValue = [&column](const std::string& key, const std::string& value, const ui::Style& valueStyle = ui::styles::text::body) {
			ui::Ref<ui::Component> line = column.addChild<ui::Component>({.layout = row(kGap)});
			line.addChild<ui::Text>({.size = {120.0F, ui::kAuto}, .layout = kKeepHeight, .style = ui::styles::text::secondary, .text = key});
			line.addChild<ui::Text>({.layout = {.flexShrink = 1.0F}, .style = valueStyle, .text = value, .wrap = true});
		};
		const auto heading = [&column](const std::string& text) {
			column.addChild<ui::Text>({.layout = kKeepHeight, .style = ui::styles::label, .text = text});
		};

		if (!def.label.empty()) {
			keyValue("label", def.label);
		}
		size_t plainFaces = 0;
		for (const assets::PartDefinition& part : item.parts) {
			plainFaces += static_cast<size_t>(std::count_if(part.faces.begin(), part.faces.end(), [](const assets::PartFace& face) {
				return !face.drawing.has_value();
			}));
		}
		// Files, not uses: a drawing on both ends of a part is one drawing.
		std::set<std::string> tops;
		std::set<std::string> sides;
		std::set<std::string> areas;
		for (const assets::DrawingUseCheck& check : item.drawingChecks) {
			switch (check.kind) {
				case assets::DrawingUseCheck::Kind::Top:
					tops.insert(check.file);
					break;
				case assets::DrawingUseCheck::Kind::Face:
					sides.insert(check.file);
					break;
				case assets::DrawingUseCheck::Kind::Area:
					areas.insert(check.file);
					break;
			}
		}
		keyValue("parts", counted(item.parts.size(), "part") + ", " + counted(item.placements.size(), "placement"));
		keyValue("drawings", counted(tops.size(), "top") + ", " + counted(sides.size(), "side") + ", " + counted(areas.size(), "area"));
		keyValue("plain faces", std::to_string(plainFaces));
		const uint8_t level = partsPreviewLevel(def, partsLevel);
		const bool	  finest = level + 1U >= def.detailLevels.size();
		keyValue(
			"level",
			def.detailLevels.empty() ? "one level" : std::format("{:g} px/m{}", def.detailLevels[level], finest ? ", the finest" : "")
		);
		const assets::ItemView front = assets::buildItemMesh(def, 0, level, assets::GameViewConfig::Get().config());
		keyValue("vertices", std::format("{} at that level, front on (turn 0)", front.vertexCount));

		// Each area and the shapes it was made of, so an artist sees which came from the XML and which from a drawing.
		heading("USE AREAS AND CLEARANCE");
		const auto areaRows = [&](const std::string& name, const assets::ItemArea& area) {
			std::string shapes;
			for (const assets::AuthoredAreaShape& shape : area.shapes) {
				shapes += (shapes.empty() ? "" : "\n") + shapeSource(shape) + (area.defaulted ? " (the default)" : "");
			}
			keyValue(name, shapes.empty() ? "none" : shapes);
		};
		for (const assets::ItemArea& area : item.useAreas) {
			areaRows(area.name.empty() ? "use area" : "use area " + area.name, area);
		}
		areaRows("clearance", item.clearance);

		// Every drawing use, so an artist sees which SVG is off and by how much.
		const float tolerance = assets::GameViewConfig::Get().config().aspectToleranceRelative;
		heading(std::format("DRAWINGS  -  PROPORTIONS WITHIN {:g}%", static_cast<double>(tolerance) * 100.0));
		if (item.drawingChecks.empty()) {
			column.addChild<ui::Text>({.layout = kKeepHeight, .style = ui::styles::text::secondary, .text = "No drawings: every face and top is plain"});
		}
		for (const assets::DrawingUseCheck& check : item.drawingChecks) {
			ui::Ref<ui::Component> entry = column.addChild<ui::Component>({.layout = flex(ui::FlexDirection::Column, 2.0F)});
			entry.addChild<ui::Text>({.layout = kKeepHeight, .style = ui::styles::text::mono, .text = check.file});
			entry.addChild<ui::Text>({
				.layout = kKeepHeight,
				.style = ui::styles::text::secondary,
				.text = std::format("{}  -  {} x {} m", check.covers, meters(check.coversMeters.x), meters(check.coversMeters.y)),
				.wrap = true,
			});
			const auto ratios = [&check](const char* verdict) {
				return std::format(
					"{}  viewBox {:.3f}, covers {:.3f}, {:.1f}% off",
					verdict,
					static_cast<double>(check.viewBoxSize.x / check.viewBoxSize.y),
					static_cast<double>(check.coversMeters.x / check.coversMeters.y),
					static_cast<double>(check.aspectDifference) * 100.0
				);
			};
			std::string		 verdict;
			const ui::Style* style = &crit;
			switch (check.verdict) {
				case assets::DrawingUseCheck::Verdict::Unread:
					verdict = "not read: see the validation issues";
					break;
				case assets::DrawingUseCheck::Verdict::Matches:
					verdict = ratios("ok");
					style = &ok;
					break;
				case assets::DrawingUseCheck::Verdict::Mismatch:
					verdict = ratios("off");
					break;
				case assets::DrawingUseCheck::Verdict::NoFill:
					verdict = "no filled shape, so no area: strokes are guides";
					break;
			}
			entry.addChild<ui::Text>({.layout = kKeepHeight, .style = *style, .text = verdict, .wrap = true});
		}

		// Every issue of the def: under each part the loader named on it, the rest under the item.
		heading("VALIDATION");
		const auto issueList = [&](const std::string& title, const std::string& part) {
			std::string text;
			bool		anyError = false;
			for (const assets::ValidationIssue& issue : assets::AssetRegistry::Get().getValidationReport().issues) {
				const bool named = std::find(issue.parts.begin(), issue.parts.end(), part) != issue.parts.end();
				if (issue.defName != defName || (part.empty() ? !issue.parts.empty() : !named)) {
					continue;
				}
				anyError = anyError || issue.severity == assets::Severity::Error;
				text += (text.empty() ? "" : "\n") + std::string(issue.severity == assets::Severity::Error ? "[error] " : "[warning] ") + issue.message;
			}
			keyValue(title, text.empty() ? "no issues" : text, text.empty() ? ok : anyError ? crit : warn);
		};
		for (const assets::PartDefinition& part : item.parts) {
			issueList(part.name, part.name);
		}
		issueList("item", "");

		definitionWell(column, false);
	}

	void AssetDetailView::setPartsTurn(uint32_t turnQuanta) {
		partsTurnQuanta = turnQuanta % assets::kTurnQuantaPerTurn;
		if (PartsPreview* shown = largePreview.get()) {
			shown->setTurn(partsTurnQuanta);
		}
		if (ui::TextInput* entry = turnEntry.get()) {
			entry->setText(degreesText(partsTurnQuanta));
		}
	}

	void AssetDetailView::applyOverlays() {
		if (PartsPreview* shown = largePreview.get()) {
			shown->setOverlays(partsOverlays);
		}
		for (const ui::Ref<PartsPreview>& cell : sheetPreviews) {
			if (PartsPreview* shown = cell.get()) {
				shown->setOverlays(partsOverlays);
			}
		}
	}

	void AssetDetailView::update(float /*dt*/) {
		if (pendingBuild) {
			pendingBuild = false;
			build();
		}
	}

	void AssetDetailView::applyVariantSeeds() {
		for (int i = 0; i < kVariants; ++i) {
			if (AssetThumbnail* thumb = variants[static_cast<std::size_t>(i)].get()) {
				thumb->setAsset(defName, procedural || ground ? variantBase + static_cast<uint32_t>(i) : 42U);
			}
		}
		if (AssetThumbnail* thumb = tiling.get()) {
			thumb->setAsset(defName, variantBase);
		}
	}

	void AssetDetailView::randomize() {
		variantBase += static_cast<uint32_t>(kVariants); // step to the next block of seeds
		applyVariantSeeds();
	}

} // namespace asset_manager
