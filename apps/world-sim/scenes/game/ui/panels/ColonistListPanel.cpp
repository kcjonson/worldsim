#include "ColonistListPanel.h"

#include <algorithm>
#include <assets/AssetRegistry.h>
#include <graphics/ClipTypes.h>
#include <primitives/Primitives.h>

namespace world_sim {

// Portrait layout constants
constexpr float kPortraitSize = 32.0F;
constexpr float kPortraitMargin = 4.0F;

ColonistListPanel::ColonistListPanel(const Args& args)
	: panelWidth(args.width),
	  itemHeight(args.itemHeight),
	  onSelectCallback(args.onColonistSelected) {

	// Pre-allocate UI element pools
	itemBackgrounds.reserve(kMaxColonists);
	itemNames.reserve(kMaxColonists);
	colonistIds.reserve(kMaxColonists);
}

void ColonistListPanel::setPosition(float x, float y) {
	panelX = x;
	panelY = y;
}

void ColonistListPanel::update(ColonistListModel& model, ecs::World& world) {
	// Update model and check if data changed
	bool dataChanged = model.refresh(world);

	// Get current selection from model
	ecs::EntityID newSelectedId = model.selectedId();
	bool selectionChanged = (newSelectedId != selectedId);
	selectedId = newSelectedId;

	if (dataChanged) {
		// Full rebuild required
		rebuildUI(model.colonists());
	} else if (selectionChanged) {
		// Just update selection highlighting (cheap)
		updateSelectionHighlight(selectedId);
	}

	// Always update mood bars (values may change even if structure doesn't)
	// Note: This is already handled by change detection in the model,
	// but mood bar visuals are cheap to update
	updateMoodBars(model.colonists());
}

void ColonistListPanel::rebuildUI(const std::vector<adapters::ColonistData>& colonists) {
	// Cache entity IDs for hit testing
	colonistIds.clear();
	for (const auto& colonist : colonists) {
		colonistIds.push_back(colonist.id);
	}

	// Ensure we have enough UI elements
	while (itemBackgrounds.size() < colonists.size() && itemBackgrounds.size() < kMaxColonists) {
		itemBackgrounds.push_back(std::make_unique<UI::Rectangle>());
		itemMoodBars.push_back(std::make_unique<UI::Rectangle>());
		itemNames.push_back(std::make_unique<UI::Text>());
	}

	// Update UI elements for each colonist
	float yOffset = panelY + kPadding;
	for (size_t i = 0; i < colonists.size() && i < kMaxColonists; ++i) {
		const auto& colonist = colonists[i];
		bool isSelected = (colonist.id == selectedId);

		// Item background
		auto& bg = itemBackgrounds[i];
		bg->position = {panelX + kPadding, yOffset};
		bg->size = {panelWidth - kPadding * 2, itemHeight - kItemSpacing};
		bg->style = {
			.fill = isSelected ? Foundation::Color(0.3F, 0.5F, 0.7F, 0.9F)
							   : Foundation::Color(0.2F, 0.2F, 0.2F, 0.8F),
			.border = Foundation::BorderStyle{
				.color = isSelected ? Foundation::Color(0.5F, 0.7F, 1.0F, 1.0F)
									: Foundation::Color(0.4F, 0.4F, 0.4F, 0.6F),
				.width = 1.0F,
				.cornerRadius = 4.0F,
			}
		};
		bg->visible = true;

		// Name text (right of portrait)
		auto& nameText = itemNames[i];
		float textX = panelX + kPadding + kPortraitSize + kPortraitMargin +
					  (panelWidth - kPadding * 2 - kPortraitSize - kPortraitMargin) / 2.0F;
		nameText->position = {textX, yOffset + (itemHeight - kItemSpacing) / 2.0F};
		nameText->text = colonist.name;
		nameText->style = {
			.color = Foundation::Color::white(),
			.fontSize = 10.0F,
			.hAlign = Foundation::HorizontalAlign::Center,
			.vAlign = Foundation::VerticalAlign::Middle,
		};
		nameText->visible = true;

		yOffset += itemHeight;
	}

	// Hide unused elements
	for (size_t i = colonists.size(); i < itemBackgrounds.size(); ++i) {
		itemBackgrounds[i]->visible = false;
		itemNames[i]->visible = false;
		if (i < itemMoodBars.size()) {
			itemMoodBars[i]->visible = false;
		}
	}

	// Create/update background panel
	float panelHeight = kPadding * 2 + static_cast<float>(colonists.size()) * itemHeight;
	if (!backgroundRect) {
		backgroundRect = std::make_unique<UI::Rectangle>();
	}
	backgroundRect->position = {panelX, panelY};
	backgroundRect->size = {panelWidth, panelHeight};
	backgroundRect->style = {
		.fill = Foundation::Color(0.1F, 0.1F, 0.1F, 0.85F),
		.border = Foundation::BorderStyle{
			.color = Foundation::Color(0.3F, 0.3F, 0.3F, 1.0F),
			.width = 1.0F,
			.cornerRadius = 6.0F,
		}
	};
	backgroundRect->zIndex = -1;
}

void ColonistListPanel::updateSelectionHighlight(ecs::EntityID newSelectedId) {
	for (size_t i = 0; i < colonistIds.size() && i < kMaxColonists; ++i) {
		bool isSelected = (colonistIds[i] == newSelectedId);

		auto& bg = itemBackgrounds[i];
		bg->style.fill = isSelected ? Foundation::Color(0.3F, 0.5F, 0.7F, 0.9F)
									: Foundation::Color(0.2F, 0.2F, 0.2F, 0.8F);
		bg->style.border->color = isSelected ? Foundation::Color(0.5F, 0.7F, 1.0F, 1.0F)
											: Foundation::Color(0.4F, 0.4F, 0.4F, 0.6F);
	}
}

void ColonistListPanel::updateMoodBars(const std::vector<adapters::ColonistData>& colonists) {
	float yOffset = panelY + kPadding;

	for (size_t i = 0; i < colonists.size() && i < kMaxColonists; ++i) {
		const auto& colonist = colonists[i];

		auto& moodBar = itemMoodBars[i];
		float moodBarWidth = panelWidth - kPadding * 2 - kPortraitSize - kPortraitMargin;
		float moodBarX = panelX + kPadding + kPortraitSize + kPortraitMargin;
		float moodBarY = yOffset + itemHeight - kItemSpacing - 6.0F;

		// Mood comes from adapter data (already computed)
		float moodRatio = std::clamp(colonist.mood / 100.0F, 0.0F, 1.0F);
		moodBar->position = {moodBarX, moodBarY};
		moodBar->size = {moodBarWidth * moodRatio, 4.0F};

		// Green->yellow->red gradient based on value
		float r = moodRatio < 0.5F ? 1.0F : 1.0F - (moodRatio - 0.5F) * 2.0F * 0.2F;
		float g = moodRatio > 0.5F ? 1.0F : 0.5F + moodRatio;
		moodBar->style = {
			.fill = Foundation::Color(r, g, 0.2F, 0.9F),
			.border = Foundation::BorderStyle{
				.color = Foundation::Color(0.0F, 0.0F, 0.0F, 0.7F),
				.width = 1.0F,
				.cornerRadius = 2.0F,
			}
		};
		moodBar->visible = true;

		yOffset += itemHeight;
	}
}

bool ColonistListPanel::handleEvent(UI::InputEvent& event) {
	if (colonistIds.empty()) {
		return false;
	}

	// Only handle mouse up (click) events
	if (event.type != UI::InputEvent::Type::MouseUp) {
		return false;
	}

	if (event.button != engine::MouseButton::Left) {
		return false;
	}

	auto pos = event.position;

	// Check if click is within panel bounds
	Foundation::Rect bounds = getBounds();
	if (pos.x < bounds.x || pos.x > bounds.x + bounds.width ||
		pos.y < bounds.y || pos.y > bounds.y + bounds.height) {
		return false;
	}

	// Find which item was clicked
	float yOffset = panelY + kPadding;
	for (size_t i = 0; i < colonistIds.size() && i < kMaxColonists; ++i) {
		float itemTop = yOffset;
		float itemBottom = yOffset + itemHeight - kItemSpacing;

		if (pos.y >= itemTop && pos.y < itemBottom) {
			// This item was clicked
			if (onSelectCallback) {
				onSelectCallback(colonistIds[i]);
			}
			event.consume();
			return true;
		}
		yOffset += itemHeight;
	}

	// Clicked in panel but not on any item - still consume to prevent world click
	event.consume();
	return true;
}

void ColonistListPanel::render() {
	if (colonistIds.empty()) {
		return;
	}

	// Render background first
	if (backgroundRect) {
		backgroundRect->render();
	}

	// Get colonist mesh template for portraits
	auto& registry = engine::assets::AssetRegistry::Get();
	const auto* colonistMesh = registry.getTemplate("Colonist_down");

	// Render items
	float yOffset = panelY + kPadding;
	for (size_t i = 0; i < colonistIds.size() && i < kMaxColonists; ++i) {
		if (itemBackgrounds[i]->visible) {
			itemBackgrounds[i]->render();
		}
		if (i < itemMoodBars.size() && itemMoodBars[i]->visible) {
			itemMoodBars[i]->render();
		}

		// Render portrait (colonist sprite, showing upper portion)
		if (colonistMesh != nullptr && !colonistMesh->vertices.empty()) {
			float portraitX = panelX + kPadding + kPortraitMargin;
			float portraitY = yOffset + (itemHeight - kItemSpacing - kPortraitSize) / 2.0F;

			// Cache mesh bounds (computed once, reused for all portraits)
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
				float displayHeight = cachedMesh.height * kCropRatio;
				cachedMesh.scale = kPortraitSize / std::max(cachedMesh.width, displayHeight);
				cachedMesh.valid = true;
			}

			// Transform vertices to screen space
			screenVerts.clear();
			screenVerts.reserve(colonistMesh->vertices.size());
			for (const auto& v : colonistMesh->vertices) {
				float sx = portraitX + (v.x - cachedMesh.minX - cachedMesh.width * 0.5F) *
							   cachedMesh.scale + kPortraitSize * 0.5F;
				float sy = portraitY + (v.y - cachedMesh.minY) * cachedMesh.scale;
				screenVerts.push_back({sx, sy});
			}

			// Clip to show only upper portion
			Foundation::ClipSettings clipSettings;
			clipSettings.shape = Foundation::ClipRect{
				.bounds = Foundation::Rect{portraitX, portraitY, kPortraitSize, kPortraitSize}
			};
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

		if (itemNames[i]->visible) {
			itemNames[i]->render();
		}

		yOffset += itemHeight;
	}
}

Foundation::Rect ColonistListPanel::getBounds() const {
	float panelHeight = kPadding * 2 + static_cast<float>(colonistIds.size()) * itemHeight;
	return {panelX, panelY, panelWidth, panelHeight};
}

} // namespace world_sim
