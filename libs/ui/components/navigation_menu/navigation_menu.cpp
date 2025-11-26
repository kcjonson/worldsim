#include "components/navigation_menu/navigation_menu.h"
#include "graphics/color.h"
#include "primitives/primitives.h"
#include "utils/log.h"

namespace UI {

	NavigationMenu::NavigationMenu(const Args& args)
		: m_sceneNames(args.sceneNames)
		, m_onSceneSelected(args.onSceneSelected)
		, m_coordinateSystem(args.coordinateSystem) {
		InitializeComponents();
	}

	NavigationMenu::~NavigationMenu() = default;

	NavigationMenu::NavigationMenu(NavigationMenu&& other) noexcept
		: m_expanded(other.m_expanded)
		, m_sceneNames(std::move(other.m_sceneNames))
		, m_onSceneSelected(std::move(other.m_onSceneSelected))
		, m_coordinateSystem(other.m_coordinateSystem)
		, m_toggleButton(std::move(other.m_toggleButton))
		, m_menuButtons(std::move(other.m_menuButtons))
		, m_headerText(std::move(other.m_headerText))
		, m_menuX(other.m_menuX)
		, m_menuY(other.m_menuY)
		, m_menuHeight(other.m_menuHeight) {
		other.m_coordinateSystem = nullptr;
	}

	NavigationMenu& NavigationMenu::operator=(NavigationMenu&& other) noexcept {
		if (this != &other) {
			m_expanded = other.m_expanded;
			m_sceneNames = std::move(other.m_sceneNames);
			m_onSceneSelected = std::move(other.m_onSceneSelected);
			m_coordinateSystem = other.m_coordinateSystem;
			m_toggleButton = std::move(other.m_toggleButton);
			m_menuButtons = std::move(other.m_menuButtons);
			m_headerText = std::move(other.m_headerText);
			m_menuX = other.m_menuX;
			m_menuY = other.m_menuY;
			m_menuHeight = other.m_menuHeight;

			other.m_coordinateSystem = nullptr;
		}
		return *this;
	}

	void NavigationMenu::InitializeComponents() {
		using namespace Foundation;

		if (m_coordinateSystem == nullptr) {
			LOG_ERROR(UI, "NavigationMenu: coordinateSystem is null");
			return;
		}

		// Get window size for bottom-right positioning
		glm::vec2 windowSize = m_coordinateSystem->GetWindowSize();

		// Toggle button position (bottom-right corner)
		float toggleX = windowSize.x - kToggleSize - kMargin;
		float toggleY = windowSize.y - kToggleSize - kMargin;

		// Create toggle button with simple icon
		m_toggleButton.emplace(
			Button::Args{
				.label = "...",
				.position = {toggleX, toggleY},
				.size = {kToggleSize, kToggleSize},
				.type = Button::Type::Primary,
				.onClick = [this]() { m_expanded = !m_expanded; },
				.zIndex = 120.0F,
				.id = "menu_toggle_button"
			}
		);

		// Calculate menu position (above the toggle button)
		m_menuHeight = kHeaderHeight + (static_cast<float>(m_sceneNames.size()) * kLineHeight);
		m_menuX = windowSize.x - kMenuWidth - kMargin;
		m_menuY = toggleY - m_menuHeight - kMenuToggleGap;

		// Create header text
		m_headerText = Text{
			.position = {m_menuX + 10, m_menuY + 8},
			.text = "Scenes",
			.style = {.color = Color(0.9F, 0.9F, 0.9F, 1.0F), .fontSize = 16.0F},
			.zIndex = 110.0F,
			.id = "menu_header_text"
		};

		// Create button for each scene
		m_menuButtons.clear();
		for (size_t i = 0; i < m_sceneNames.size(); i++) {
			float itemY = m_menuY + kHeaderHeight + (static_cast<float>(i) * kLineHeight);

			// Capture scene name by value for onClick callback
			std::string sceneName = m_sceneNames[i];

			m_menuButtons.push_back(
				Button{Button::Args{
					.label = sceneName,
					.position = {m_menuX + 2, itemY + 2},
					.size = {kMenuWidth - 4, kLineHeight - 4},
					.type = Button::Type::Secondary,
					.onClick =
						[this, sceneName]() {
							if (m_onSceneSelected) {
								m_onSceneSelected(sceneName);
							}
							m_expanded = false; // Close menu after selection
						},
					.zIndex = 100.0F,
					.id = ("menu_button_" + std::to_string(i)).c_str()
				}}
			);
		}
	}

	void NavigationMenu::OnWindowResize() {
		InitializeComponents();
	}

	void NavigationMenu::HandleInput() {
		// Always handle the toggle button
		if (m_toggleButton) {
			m_toggleButton->HandleInput();
			m_toggleButton->Update(0.0F);
		}

		// Only handle menu buttons when expanded
		if (m_expanded) {
			for (auto& button : m_menuButtons) {
				button.HandleInput();
				button.Update(0.0F);
			}
		}
	}

	void NavigationMenu::Update(float /*deltaTime*/) {
		// Currently no time-based updates needed
	}

	void NavigationMenu::Render() const {
		using namespace Foundation;

		// Always render the toggle button
		if (m_toggleButton) {
			m_toggleButton->Render();
		}

		// Only render menu when expanded
		if (!m_expanded) {
			return;
		}

		// Draw menu background
		Renderer::Primitives::DrawRect(
			{.bounds = {m_menuX, m_menuY, kMenuWidth, m_menuHeight},
			 .style = {.fill = Color(0.15F, 0.15F, 0.2F, 0.95F), .border = BorderStyle{.color = Color(0.4F, 0.4F, 0.5F, 1.0F), .width = 1.0F}},
			 .id = "menu_background"}
		);

		// Draw header background
		Renderer::Primitives::DrawRect(
			{.bounds = {m_menuX, m_menuY, kMenuWidth, kHeaderHeight}, .style = {.fill = Color(0.2F, 0.2F, 0.3F, 1.0F)}, .id = "menu_header"}
		);

		// Render header text
		m_headerText.Render();

		// Render all menu buttons
		for (const auto& button : m_menuButtons) {
			button.Render();
		}
	}

} // namespace UI
