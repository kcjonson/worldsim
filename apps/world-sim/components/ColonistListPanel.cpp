#include "ColonistListPanel.h"

#include <assets/AssetRegistry.h>
#include <graphics/ClipTypes.h>
#include <input/InputManager.h>
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
	}

	void ColonistListPanel::setPosition(float x, float y) {
		panelX = x;
		panelY = y;
	}

	void ColonistListPanel::update(ecs::World& world, ecs::EntityID selectedColonistId) {
		selectedId = selectedColonistId;

		// Query ECS for all colonists
		colonists.clear();
		for (auto [entity, colonist] : world.view<ecs::Colonist>()) {
			colonists.push_back({entity, colonist.name});
		}

		// Ensure we have enough UI elements
		while (itemBackgrounds.size() < colonists.size() && itemBackgrounds.size() < kMaxColonists) {
			itemBackgrounds.push_back(std::make_unique<UI::Rectangle>());
			itemNames.push_back(std::make_unique<UI::Text>());
		}

		// Update UI elements for each colonist
		float yOffset = panelY + kPadding;
		for (size_t i = 0; i < colonists.size() && i < kMaxColonists; ++i) {
			const auto& colonist = colonists[i];
			bool		isSelected = (colonist.entityId == selectedId);

			// Item background
			auto& bg = itemBackgrounds[i];
			bg->position = {panelX + kPadding, yOffset};
			bg->size = {panelWidth - kPadding * 2, itemHeight - kItemSpacing};
			bg->style = {
				.fill = isSelected ? Foundation::Color(0.3F, 0.5F, 0.7F, 0.9F)	// Selected: blue
								   : Foundation::Color(0.2F, 0.2F, 0.2F, 0.8F), // Normal: dark gray
				.border = Foundation::BorderStyle{
					.color = isSelected ? Foundation::Color(0.5F, 0.7F, 1.0F, 1.0F) : Foundation::Color(0.4F, 0.4F, 0.4F, 0.6F),
					.width = 1.0F,
					.cornerRadius = 4.0F,
				}
			};
			bg->visible = true;

			// Name text (right of portrait)
			auto& nameText = itemNames[i];
			float textX =
				panelX + kPadding + kPortraitSize + kPortraitMargin + (panelWidth - kPadding * 2 - kPortraitSize - kPortraitMargin) / 2.0F;
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
		backgroundRect->zIndex = -1; // Behind items
	}

	bool ColonistListPanel::handleInput() {
		if (colonists.empty()) {
			return false;
		}

		auto& input = engine::InputManager::Get();
		// Use isMouseButtonReleased to match GameScene's input handling
		// This prevents the click from being processed twice
		if (!input.isMouseButtonReleased(engine::MouseButton::Left)) {
			return false;
		}

		glm::vec2 mousePos = input.getMousePosition();

		// Check if click is within panel bounds
		Foundation::Rect bounds = getBounds();
		if (mousePos.x < bounds.x || mousePos.x > bounds.x + bounds.width || mousePos.y < bounds.y ||
			mousePos.y > bounds.y + bounds.height) {
			return false;
		}

		// Find which item was clicked
		float yOffset = panelY + kPadding;
		for (size_t i = 0; i < colonists.size() && i < kMaxColonists; ++i) {
			float itemTop = yOffset;
			float itemBottom = yOffset + itemHeight - kItemSpacing;

			if (mousePos.y >= itemTop && mousePos.y < itemBottom) {
				// This item was clicked
				if (onSelectCallback) {
					onSelectCallback(colonists[i].entityId);
				}
				return true;
			}
			yOffset += itemHeight;
		}

		return true; // Consumed (clicked in panel but not on item)
	}

	void ColonistListPanel::render() {
		if (colonists.empty()) {
			return;
		}

		// Render background first
		if (backgroundRect) {
			backgroundRect->render();
		}

		// Get colonist mesh template for portraits
		auto&		registry = engine::assets::AssetRegistry::Get();
		const auto* colonistMesh = registry.getTemplate("Colonist_down");

		// Render items
		float yOffset = panelY + kPadding;
		for (size_t i = 0; i < colonists.size() && i < kMaxColonists; ++i) {
			if (itemBackgrounds[i]->visible) {
				itemBackgrounds[i]->render();
			}

			// Render portrait (colonist sprite, showing upper portion - head and shoulders)
			if (colonistMesh != nullptr && !colonistMesh->vertices.empty()) {
				// Portrait position and size
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

					// Show upper 55% of sprite (head and shoulders)
					constexpr float kCropRatio = 0.55F;
					float			displayHeight = cachedMesh.height * kCropRatio;
					cachedMesh.scale = kPortraitSize / std::max(cachedMesh.width, displayHeight);
					cachedMesh.valid = true;
				}

				// Transform vertices to screen space (reuse buffer)
				screenVerts.clear();
				screenVerts.reserve(colonistMesh->vertices.size());
				for (const auto& v : colonistMesh->vertices) {
					// Center horizontally, align to top
					float sx = portraitX + (v.x - cachedMesh.minX - cachedMesh.width * 0.5F) * cachedMesh.scale + kPortraitSize * 0.5F;
					float sy = portraitY + (v.y - cachedMesh.minY) * cachedMesh.scale;
					screenVerts.push_back({sx, sy});
				}

				// Apply clipping to show only the upper portion (head and shoulders)
				Foundation::ClipSettings clipSettings;
				clipSettings.shape = Foundation::ClipRect{.bounds = Foundation::Rect{portraitX, portraitY, kPortraitSize, kPortraitSize}};
				clipSettings.mode = Foundation::ClipMode::Inside;
				Renderer::Primitives::pushClip(clipSettings);

				// Draw the mesh
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
		float panelHeight = kPadding * 2 + static_cast<float>(colonists.size()) * itemHeight;
		return {panelX, panelY, panelWidth, panelHeight};
	}

} // namespace world_sim
