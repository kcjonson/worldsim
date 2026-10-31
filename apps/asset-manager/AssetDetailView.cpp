#include "AssetDetailView.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <theme/Tokens.h>
#include <ui/Context.h>
#include <ui/components/Badge.h>
#include <ui/components/Button.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>
#include <ui/theme/Tokens.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace asset_manager {

	namespace {

		constexpr float kPad = 16.0F;
		constexpr float kGap = 12.0F;
		constexpr float kInfoWidth = 360.0F;

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
		constexpr ui::Layout kKeepHeight{.flexShrink = 0.0F};
		constexpr ui::Layout kHug{.flexShrink = 0.0F, .alignSelf = ui::AlignItems::Start};

	} // namespace

	AssetDetailView::AssetDetailView(const Args& /*args*/) {}

	void AssetDetailView::onMount() {
		build();
	}

	void AssetDetailView::setAsset(const std::string& name) {
		const engine::assets::AssetDefinition* def = engine::assets::AssetRegistry::Get().getDefinition(name);
		defName = def != nullptr ? name : std::string();
		procedural = def != nullptr && def->assetType != engine::assets::AssetType::Simple;
		variantBase = 1U;
		if (getContext() != nullptr) {
			build();
		}
	}

	void AssetDetailView::build() {
		if (content.get() != nullptr) {
			removeChild(content);
		}
		variants = {};
		content = addChild<ui::Component>({
			.size = {ui::percent(100.0F), ui::percent(100.0F)},
			.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = kGap, .padding = kPad},
		});
		const engine::assets::AssetDefinition* def = engine::assets::AssetRegistry::Get().getDefinition(defName);
		if (def == nullptr) {
			content.addChild<ui::Text>(
				{.layout = kKeepHeight, .style = ui::styles::text::secondary, .text = "Select an asset from the tree"}
			);
			return;
		}

		const std::string category = def->baseFolder.empty() ? "asset" : def->baseFolder.parent_path().filename().string();
		content.addChild<ui::Text>({.layout = kKeepHeight, .style = ui::styles::label, .text = toUpper(category)});
		content.addChild<ui::Text>({.layout = kKeepHeight, .style = ui::styles::text::title, .text = defName});
		content.addChild<ui::Badge>(
			{.layout = kHug, .label = procedural ? "PROCEDURAL" : "SIMPLE", .tone = procedural ? ui::Tone::Accent : ui::Tone::Info}
		);

		ui::Ref<ui::Component> columns = content.addChild<ui::Component>({.layout = flex(ui::FlexDirection::Row, 20.0F, 1.0F)});
		preview(columns.addChild<ui::Component>({.layout = flex(ui::FlexDirection::Column, kGap, 1.0F)}));
		info(columns.addChild<ui::Component>({.size = {kInfoWidth, ui::kAuto}, .layout = flex(ui::FlexDirection::Column, kGap)}));
	}

	// One framed thumbnail, or six in a 2x3 grid under Randomize; the collider over the first.
	void AssetDetailView::preview(ui::Ref<ui::Component> column) {
		const auto cell = [this](ui::Ref<ui::Component> parent, int index) {
			ui::Ref<ui::Rectangle> frame =
				parent.addChild<ui::Rectangle>({.layout = {.padding = 8.0F, .flexGrow = 1.0F}, .style = ui::styles::inset});
			variants[static_cast<std::size_t>(index)] = frame.addChild<AssetThumbnail>({
				.size = {ui::percent(100.0F), ui::percent(100.0F)},
				.animated = true,
				.collider = index == 0,
			});
		};
		if (!procedural) {
			cell(column, 0);
		} else {
			column.addChild<ui::Button>({.layout = kHug, .label = "Randomize", .controlSize = ui::ControlSize::Sm, .onPress = [this] {
											 randomize();
										 }});
			for (int row = 0; row < 3; ++row) {
				ui::Ref<ui::Component> pair = column.addChild<ui::Component>({.layout = flex(ui::FlexDirection::Row, kGap, 1.0F)});
				cell(pair, row * 2);
				cell(pair, (row * 2) + 1);
			}
		}
		applyVariantSeeds();
	}

	// The asset's facts, its validation issues, and its source XML.
	void AssetDetailView::info(ui::Ref<ui::Component> column) {
		const engine::assets::AssetDefinition& def = *engine::assets::AssetRegistry::Get().getDefinition(defName);
		const ui::Theme&					   palette = getContext()->getTheme();

		std::string meta;
		if (!def.label.empty()) {
			meta += def.label + "\n";
		}
		meta += "worldHeight  " + fmtFloat(def.worldHeight);
		if (!def.svgPath.empty()) {
			meta += "\nsvg  " + def.svgPath;
		}
		if (!def.scriptPath.empty()) {
			meta += "\nscript  " + def.scriptPath;
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
		for (const auto& issue : engine::assets::AssetRegistry::Get().getValidationReport().issues) {
			if (issue.defName != defName) {
				continue;
			}
			const bool isError = issue.severity == engine::assets::Severity::Error;
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

		const std::filesystem::path xmlPath = def.baseFolder / (def.baseFolder.filename().string() + ".xml");
		column.addChild<ui::Text>(
			{.layout = kKeepHeight, .style = ui::styles::label, .text = toUpper("Definition  -  " + xmlPath.filename().string())}
		);
		ui::Ref<ui::Rectangle> well = column.addChild<ui::Rectangle>({
			.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .padding = kGap, .flexGrow = 1.0F},
			.style = ui::styles::inset,
			.overflow = ui::Overflow::Scroll,
		});
		// Unwrapped, as the file reads: a long path has nowhere to break, so the well scrolls sideways.
		well.addChild<ui::Text>(
			{.layout = {.alignSelf = ui::AlignItems::Start}, .style = ui::styles::text::mono, .text = readFile(xmlPath)}
		);
	}

	void AssetDetailView::applyVariantSeeds() {
		for (int i = 0; i < kVariants; ++i) {
			if (AssetThumbnail* thumb = variants[static_cast<std::size_t>(i)].get()) {
				thumb->setAsset(defName, procedural ? variantBase + static_cast<uint32_t>(i) : 42U);
			}
		}
	}

	void AssetDetailView::randomize() {
		variantBase += static_cast<uint32_t>(kVariants); // step to the next block of seeds
		applyVariantSeeds();
	}

} // namespace asset_manager
