#include "navigation_menu.h"
#include "graphics/color.h"
#include "primitives/primitives.h"
#include "utils/log.h"

namespace UI {

	NavigationMenu::NavigationMenu(const Args& args)
		: sceneNames(args.sceneNames),
		  onSceneSelected(args.onSceneSelected),
		  coordinateSystem(args.coordinateSystem) {
		initializeComponents();
	}

	NavigationMenu::~NavigationMenu() = default;

	NavigationMenu::NavigationMenu(NavigationMenu&& other) noexcept
		: expanded(other.expanded),
		  sceneNames(std::move(other.sceneNames)),
		  onSceneSelected(std::move(other.onSceneSelected)),
		  coordinateSystem(other.coordinateSystem),
		  toggleButton(std::move(other.toggleButton)),
		  menuButtons(std::move(other.menuButtons)),
		  buttonIds(std::move(other.buttonIds)),
		  headerText(std::move(other.headerText)),
		  menuX(other.menuX),
		  menuY(other.menuY),
		  menuHeight(other.menuHeight) {
		other.coordinateSystem = nullptr;
	}

	NavigationMenu& NavigationMenu::operator=(NavigationMenu&& other) noexcept {
		if (this != &other) {
			expanded = other.expanded;
			sceneNames = std::move(other.sceneNames);
			onSceneSelected = std::move(other.onSceneSelected);
			coordinateSystem = other.coordinateSystem;
			toggleButton = std::move(other.toggleButton);
			menuButtons = std::move(other.menuButtons);
			buttonIds = std::move(other.buttonIds);
			headerText = std::move(other.headerText);
			menuX = other.menuX;
			menuY = other.menuY;
			menuHeight = other.menuHeight;

			other.coordinateSystem = nullptr;
		}
		return *this;
	}

	void NavigationMenu::initializeComponents() {
		using namespace Foundation;

		if (coordinateSystem == nullptr) {
			LOG_ERROR(UI, "NavigationMenu: coordinateSystem is null");
			return;
		}

		// Get window size for bottom-right positioning
		glm::vec2 windowSize = coordinateSystem->GetWindowSize();

		// Toggle button position (bottom-right corner)
		float toggleX = windowSize.x - kToggleSize - kMargin;
		float toggleY = windowSize.y - kToggleSize - kMargin;

		// Create toggle button with simple icon
		toggleButton.emplace(
			Button::Args{
				.label = "...",
				.position = {toggleX, toggleY},
				.size = {kToggleSize, kToggleSize},
				.type = Button::Type::Primary,
				.onClick = [this]() { expanded = !expanded; },
				.id = "menu_toggle_button"
			}
		);

		// Calculate menu position (above the toggle button)
		menuHeight = kHeaderHeight + (static_cast<float>(sceneNames.size()) * kLineHeight);
		menuX = windowSize.x - kMenuWidth - kMargin;
		menuY = toggleY - menuHeight - kMenuToggleGap;

		// Create header text
		headerText = Text(
			Text::Args{
				.position = {menuX + 10, menuY + 8},
				.text = "Scenes",
				.style = {.color = Color(0.9F, 0.9F, 0.9F, 1.0F), .fontSize = 16.0F},
				.id = "menu_header_text"
			}
		);

		// Create button for each scene
		menuButtons.clear();
		buttonIds.clear();
		buttonIds.reserve(sceneNames.size());
		for (size_t i = 0; i < sceneNames.size(); i++) {
			float itemY = menuY + kHeaderHeight + (static_cast<float>(i) * kLineHeight);

			// Capture scene name by value for onClick callback
			std::string sceneName = sceneNames[i];

			// Store button ID to avoid dangling pointer from temporary string
			buttonIds.push_back("menu_button_" + std::to_string(i));

			menuButtons.push_back(
				Button{Button::Args{
					.label = sceneName,
					.position = {menuX + 2, itemY + 2},
					.size = {kMenuWidth - 4, kLineHeight - 4},
					.type = Button::Type::Secondary,
					.onClick =
						[this, sceneName]() {
							if (onSceneSelected) {
								onSceneSelected(sceneName);
							}
							expanded = false; // Close menu after selection
						},
					.id = buttonIds.back().c_str()
				}}
			);
		}
	}

	void NavigationMenu::onWindowResize() {
		initializeComponents();
	}

	void NavigationMenu::handleInput() {
		// Always handle the toggle button
		if (toggleButton) {
			toggleButton->handleInput();
		}

		// Only handle menu buttons when expanded
		if (expanded) {
			for (auto& button : menuButtons) {
				button.handleInput();
			}
		}
	}

	void NavigationMenu::update(float deltaTime) {
		// Update toggle button
		if (toggleButton) {
			toggleButton->update(deltaTime);
		}

		// Update menu buttons if expanded
		if (expanded) {
			for (auto& button : menuButtons) {
				button.update(deltaTime);
			}
		}
	}

	void NavigationMenu::render() {
		using namespace Foundation;

		// Always render the toggle button
		if (toggleButton) {
			toggleButton->render();
		}

		// Only render menu when expanded
		if (!expanded) {
			return;
		}

		// Draw menu background
		Renderer::Primitives::drawRect(
			{.bounds = {menuX, menuY, kMenuWidth, menuHeight},
			 .style =
				 {.fill = Color(0.15F, 0.15F, 0.2F, 0.95F), .border = BorderStyle{.color = Color(0.4F, 0.4F, 0.5F, 1.0F), .width = 1.0F}},
			 .id = "menu_background"}
		);

		// Draw header background
		Renderer::Primitives::drawRect(
			{.bounds = {menuX, menuY, kMenuWidth, kHeaderHeight}, .style = {.fill = Color(0.2F, 0.2F, 0.3F, 1.0F)}, .id = "menu_header"}
		);

		// Render header text
		headerText.render();

		// Render all menu buttons
		for (auto& button : menuButtons) {
			button.render();
		}
	}

} // namespace UI
