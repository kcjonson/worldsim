#include "app/UiBridge.h"

#include "app/Keys.h"
#include "app/UiScene.h"

#include <debug/LayoutLint.h>
#include <debug/UiTreeSerializer.h>
#include <input/InputEvent.h>

#include <application/App.h>

#include <array>
#include <cstddef>

namespace engine {

	namespace {

		using foundation::Key;
		using foundation::MouseButton;

		constexpr std::array kButtons{MouseButton::Left, MouseButton::Right};

		// UI::InputEvent's modifier bits (GLFW's), from the keys held now.
		int heldModifiers(const foundation::InputSnapshot& input) {
			const auto held = [&input](Key left, Key right) {
				return key(input, left).down || key(input, right).down;
			};
			int mods = 0;
			if (held(Key::LeftShift, Key::RightShift)) {
				mods |= foundation::ModShift;
			}
			if (held(Key::LeftControl, Key::RightControl)) {
				mods |= foundation::ModControl;
			}
			if (held(Key::LeftAlt, Key::RightAlt)) {
				mods |= foundation::ModAlt;
			}
			return mods;
		}

		// A button's edges since the last frame, at most two: an odd count of transitions gives the one
		// edge to where it is now, an even count the edge away and the one back.
		struct Edges {
			bool down = false;
			bool up = false;
			bool upFirst = false; // released and pressed again
		};

		Edges edgesOf(const foundation::ButtonState& button) {
			if (button.transitions == 0) {
				return {};
			}
			if (button.transitions % 2 == 1) {
				return {.down = button.down, .up = !button.down};
			}
			return {.down = true, .up = true, .upFirst = button.down};
		}

	} // namespace

	UiBridge::UiBridge(app::App& app)
		: owner(app),
		  clipboard(app.clipboard()) {
		UI::FocusManager::setInstance(&focus);
		UI::IClipboard::setInstance(&clipboard);
		owner.setDispatch([this](app::App& /*app*/) { dispatch(); });

		const auto viewport = [this] {
			const Renderer::Viewport& window = owner.viewport();
			return Foundation::Vec2{static_cast<float>(window.logicalWidth), static_cast<float>(window.logicalHeight)};
		};
		uiTree = owner.devServer().stateProvider("ui.tree", [this, viewport] { return UI::serializeUiTreeJson(uiRoots(), viewport()); });
		uiLint = owner.devServer().stateProvider("ui.lint", [this, viewport] { return UI::lintUiTreeJson(uiRoots(), viewport()); });
	}

	UiBridge::~UiBridge() {
		owner.setDispatch({});
	}

	void UiBridge::addOverlay(UiOverlay& overlay) {
		overlays.push_back(&overlay);
	}

	float UiBridge::consumeScroll() {
		const float delta = scrollDelta;
		scrollDelta = 0.0F;
		return delta;
	}

	void UiBridge::dispatch() {
		for (const foundation::InputEvent& event : owner.input().events()) {
			switch (event.type) {
				case foundation::InputEvent::Type::KeyDown:
					routeKey(event);
					break;
				case foundation::InputEvent::Type::Char:
					focus.routeCharInput(event.codepoint);
					break;
				case foundation::InputEvent::Type::Scroll:
					scrollDelta = event.scroll.y;
					break;
				default:
					break;
			}
		}
		if (!owner.paused()) {
			dispatchPointer(owner.input().frame());
		}
	}

	// A press or a repeat; Tab and Shift-Tab move the focus.
	void UiBridge::routeKey(const foundation::InputEvent& event) {
		const bool shift = (event.mods & foundation::ModShift) != 0;
		if (event.key == Key::Tab) {
			if (shift) {
				focus.focusPrevious();
			} else {
				focus.focusNext();
			}
			return;
		}
		bool ctrl = (event.mods & foundation::ModControl) != 0;
#ifdef __APPLE__
		// Cmd is the shortcut key on macOS (Cmd+C, Cmd+V, Cmd+X)
		ctrl = ctrl || (event.mods & foundation::ModSuper) != 0;
#endif
		const bool alt = (event.mods & foundation::ModAlt) != 0;
		focus.routeKeyInput(event.key, shift, ctrl, alt);
	}

	void UiBridge::dispatchPointer(const foundation::InputSnapshot& input) {
		const Foundation::Vec2 position = input.pointer;
		const int			   mods = heldModifiers(input);

		UI::InputEvent move = UI::InputEvent::mouseMove(position, mods);
		send(move);

		// Presses, then releases.
		for (const MouseButton button : kButtons) {
			const Edges edges = edgesOf(input.mouse[static_cast<std::size_t>(button)]);
			if (!edges.down) {
				continue;
			}
			if (edges.upFirst) {
				UI::InputEvent up = UI::InputEvent::mouseUp(position, button, mods);
				send(up);
			}
			UI::InputEvent down = UI::InputEvent::mouseDown(position, button, mods);
			send(down);
		}
		for (const MouseButton button : kButtons) {
			const Edges edges = edgesOf(input.mouse[static_cast<std::size_t>(button)]);
			if (edges.up && !edges.upFirst) {
				UI::InputEvent up = UI::InputEvent::mouseUp(position, button, mods);
				send(up);
			}
		}

		if (scrollDelta != 0.0F) {
			UI::InputEvent scroll = UI::InputEvent::scroll(position, scrollDelta);
			if (send(scroll) || scroll.isConsumed()) {
				scrollDelta = 0.0F;
			}
		}
	}

	bool UiBridge::send(UI::InputEvent& event) {
		for (auto it = overlays.rbegin(); it != overlays.rend(); ++it) {
			if ((*it)->handleInput(event)) {
				return true;
			}
		}
		auto* scene = static_cast<UiScene*>(owner.scenes().top());
		return scene != nullptr && scene->handleInput(event);
	}

	std::vector<const UI::IComponent*> UiBridge::uiRoots() const {
		const auto* scene = static_cast<const UiScene*>(owner.scenes().top());
		return scene != nullptr ? scene->getUiRoots() : std::vector<const UI::IComponent*>{};
	}

} // namespace engine
