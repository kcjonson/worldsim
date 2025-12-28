#include "ColonistListItem.h"

#include <algorithm>
#include <assets/AssetRegistry.h>
#include <graphics/ClipTypes.h>
#include <primitives/Primitives.h>
#include <theme/Theme.h>

namespace world_sim {

	// Static member definition
	ColonistListItem::CachedMeshData ColonistListItem::cachedMesh;

	ColonistListItem::ColonistListItem(const Args& args)
		: entityId(args.colonist.id),
		  name(args.colonist.name),
		  mood(args.colonist.mood),
		  selected(args.isSelected),
		  onSelect(args.onSelect) {

		// Set component size and margin for layout
		size = {args.width, args.height};
		margin = args.itemMargin;

		// Background rectangle
		backgroundHandle = addChild(
			UI::Rectangle(
				UI::Rectangle::Args{
					.position = {0.0F, 0.0F},
					.size = size,
					.style =
						{.fill = selected ? UI::Theme::Colors::selectionBackground : UI::Theme::Colors::cardBackground,
						 .border =
							 Foundation::BorderStyle{
								 .color = selected ? UI::Theme::Colors::selectionBorder : UI::Theme::Colors::cardBorder,
								 .width = 1.0F,
								 .cornerRadius = 4.0F,
							 }},
					.id = (args.id + "_bg").c_str()
				}
			)
		);

		// Name text (right of portrait, centered)
		float textX = kPortraitSize + kPortraitMargin + (size.x - kPortraitSize - kPortraitMargin) / 2.0F;
		nameTextHandle = addChild(
			UI::Text(
				UI::Text::Args{
					.position = {textX, size.y / 2.0F},
					.text = name,
					.style =
						{
							.color = UI::Theme::Colors::textTitle,
							.fontSize = 10.0F,
							.hAlign = Foundation::HorizontalAlign::Center,
							.vAlign = Foundation::VerticalAlign::Middle,
						},
					.id = (args.id + "_name").c_str()
				}
			)
		);

		// Mood bar
		float moodBarWidth = size.x - kPortraitSize - kPortraitMargin;
		float moodBarX = kPortraitSize + kPortraitMargin;
		float moodBarY = size.y - kMoodBarOffset;
		float moodRatio = std::clamp(mood / 100.0F, 0.0F, 1.0F);

		// Mood bar color gradient (green → yellow → red based on value)
		float r = moodRatio < 0.5F ? 1.0F : 1.0F - (moodRatio - 0.5F) * 2.0F * 0.2F;
		float g = moodRatio > 0.5F ? 1.0F : 0.5F + moodRatio;

		moodBarHandle = addChild(
			UI::Rectangle(
				UI::Rectangle::Args{
					.position = {moodBarX, moodBarY},
					.size = {moodBarWidth * moodRatio, kMoodBarHeight},
					.style =
						{.fill = Foundation::Color(r, g, 0.2F, 0.9F),
						 .border =
							 Foundation::BorderStyle{
								 .color = Foundation::Color(0.0F, 0.0F, 0.0F, 0.7F),
								 .width = 1.0F,
								 .cornerRadius = 2.0F,
							 }},
					.id = (args.id + "_mood").c_str()
				}
			)
		);
	}

	void ColonistListItem::setPosition(float x, float y) {
		Component::setPosition(x, y);

		// Immediately update child positions to avoid one-frame delay
		Foundation::Vec2 contentPos = getContentPosition();
		if (auto* bg = getChild<UI::Rectangle>(backgroundHandle)) {
			bg->position = contentPos;
		}
		if (auto* nameText = getChild<UI::Text>(nameTextHandle)) {
			float textX = contentPos.x + kPortraitSize + kPortraitMargin + (size.x - kPortraitSize - kPortraitMargin) / 2.0F;
			nameText->position = {textX, contentPos.y + size.y / 2.0F};
		}
		if (auto* moodBar = getChild<UI::Rectangle>(moodBarHandle)) {
			float moodBarX = contentPos.x + kPortraitSize + kPortraitMargin;
			float moodBarY = contentPos.y + size.y - kMoodBarOffset;
			moodBar->position = {moodBarX, moodBarY};
		}
	}

	void ColonistListItem::render() {
		// Render children (background, mood bar, name)
		Component::render();

		// Render portrait last (on top of background)
		renderPortrait();
	}

	void ColonistListItem::renderPortrait() {
		auto&		registry = engine::assets::AssetRegistry::Get();
		const auto* colonistMesh = registry.getTemplate("Colonist_down");

		if (colonistMesh == nullptr || colonistMesh->vertices.empty()) {
			return;
		}

		Foundation::Vec2 contentPos = getContentPosition();
		float			 portraitX = contentPos.x + kPortraitMargin;
		float			 portraitY = contentPos.y + (size.y - kPortraitSize) / 2.0F;

		// Cache mesh bounds (computed once, reused for all items)
		if (!cachedMesh.valid) {
			cachedMesh.minX = colonistMesh->vertices[0].x;
			cachedMesh.maxX = colonistMesh->vertices[0].x;
			cachedMesh.minY = colonistMesh->vertices[0].y;
			cachedMesh.maxY = colonistMesh->vertices[0].y;
			for (const auto& v : colonistMesh->vertices) {
				cachedMesh.minX = std::min(cachedMesh.minX, v.x);
				cachedMesh.maxX = std::max(cachedMesh.maxX, v.x);
				cachedMesh.minY = std::min(cachedMesh.minY, v.y);
				cachedMesh.maxY = std::max(cachedMesh.maxY, v.y);
			}
			cachedMesh.width = cachedMesh.maxX - cachedMesh.minX;
			cachedMesh.height = cachedMesh.maxY - cachedMesh.minY;

			constexpr float kCropRatio = 0.55F;
			float			displayHeight = cachedMesh.height * kCropRatio;
			cachedMesh.scale = kPortraitSize / std::max(cachedMesh.width, displayHeight);
			cachedMesh.valid = true;
		}

		// Transform vertices to screen space
		screenVerts.clear();
		screenVerts.reserve(colonistMesh->vertices.size());
		for (const auto& v : colonistMesh->vertices) {
			float sx = portraitX + (v.x - cachedMesh.minX - cachedMesh.width * 0.5F) * cachedMesh.scale + kPortraitSize * 0.5F;
			float sy = portraitY + (v.y - cachedMesh.minY) * cachedMesh.scale;
			screenVerts.push_back({sx, sy});
		}

		// Clip to show only upper portion
		Foundation::ClipSettings clipSettings;
		clipSettings.shape = Foundation::ClipRect{.bounds = Foundation::Rect{portraitX, portraitY, kPortraitSize, kPortraitSize}};
		clipSettings.mode = Foundation::ClipMode::Inside;
		Renderer::Primitives::pushClip(clipSettings);

		Renderer::Primitives::drawTriangles(
			Renderer::Primitives::TrianglesArgs{
				.vertices = screenVerts.data(),
				.indices = colonistMesh->indices.data(),
				.vertexCount = screenVerts.size(),
				.indexCount = colonistMesh->indices.size(),
				.colors = colonistMesh->colors.data()
			}
		);

		Renderer::Primitives::popClip();
	}

	bool ColonistListItem::handleEvent(UI::InputEvent& event) {
		if (event.type != UI::InputEvent::Type::MouseUp) {
			return false;
		}

		if (event.button != engine::MouseButton::Left) {
			return false;
		}

		if (!containsPoint(event.position)) {
			return false;
		}

		// Item was clicked
		if (onSelect) {
			onSelect(entityId);
		}
		event.consume();
		return true;
	}

	bool ColonistListItem::containsPoint(Foundation::Vec2 point) const {
		Foundation::Vec2 contentPos = getContentPosition();
		return point.x >= contentPos.x && point.x <= contentPos.x + size.x && point.y >= contentPos.y && point.y <= contentPos.y + size.y;
	}

	void ColonistListItem::setSelected(bool newSelected) {
		if (selected == newSelected) {
			return;
		}
		selected = newSelected;
		updateBackgroundStyle();
	}

	void ColonistListItem::setMood(float newMood) {
		mood = newMood;
		updateMoodBar();
		updateBackgroundStyle();  // Mood affects background tint
	}

	void ColonistListItem::setColonistData(const adapters::ColonistData& data) {
		entityId = data.id;
		name = data.name;
		mood = data.mood;

		if (auto* nameText = getChild<UI::Text>(nameTextHandle)) {
			nameText->text = name;
		}
		updateMoodBar();
		updateBackgroundStyle();  // Mood affects background tint
	}

	void ColonistListItem::updateBackgroundStyle() {
		if (auto* bg = getChild<UI::Rectangle>(backgroundHandle)) {
			if (selected) {
				bg->style.fill = UI::Theme::Colors::selectionBackground;
			} else {
				// Apply mood-based tint to background
				bg->style.fill = getMoodTintedBackground();
			}
			if (bg->style.border) {
				bg->style.border->color = selected ? UI::Theme::Colors::selectionBorder : UI::Theme::Colors::cardBorder;
			}
		}
	}

	Foundation::Color ColonistListItem::getMoodTintedBackground() const {
		// Subtle mood-based tinting of the card background
		Foundation::Color base = UI::Theme::Colors::cardBackground;

		if (mood > 70.0F) {
			// Green tint - happy
			return Foundation::Color{
				base.r,
				base.g + 0.05F,
				base.b,
				base.a};
		} else if (mood > 40.0F) {
			// Yellow tint - neutral
			return Foundation::Color{
				base.r + 0.03F,
				base.g + 0.03F,
				base.b,
				base.a};
		} else {
			// Red tint - stressed
			return Foundation::Color{
				base.r + 0.08F,
				base.g,
				base.b,
				base.a};
		}
	}

	void ColonistListItem::updateMoodBar() {
		if (auto* moodBar = getChild<UI::Rectangle>(moodBarHandle)) {
			float moodBarWidth = size.x - kPortraitSize - kPortraitMargin;
			float moodRatio = std::clamp(mood / 100.0F, 0.0F, 1.0F);

			moodBar->size.x = moodBarWidth * moodRatio;

			// Update color gradient
			float r = moodRatio < 0.5F ? 1.0F : 1.0F - (moodRatio - 0.5F) * 2.0F * 0.2F;
			float g = moodRatio > 0.5F ? 1.0F : 0.5F + moodRatio;
			moodBar->style.fill = Foundation::Color(r, g, 0.2F, 0.9F);
		}
	}

} // namespace world_sim
