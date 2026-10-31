#include "app/UiBridge.h"

#include "app/Keys.h"
#include "app/UiScene.h"

#include <debug/UiLayoutLint.h>
#include <debug/UiTreeSerializer.h>
#include <input/InputEvent.h>

#include <application/App.h>


namespace engine {

	namespace {

		using foundation::Key;
		using foundation::MouseButton;
		using EventType = foundation::InputEvent::Type;

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

	float UiBridge::consumeScroll() {
		const float delta = scrollDelta;
		scrollDelta = 0.0F;
		return delta;
	}

	void UiBridge::dispatch() {
		const bool paused = owner.paused();
		const int  mods = heldModifiers(owner.input().frame());
		bool	   scrolled = false;
		for (const foundation::InputEvent& event : owner.input().events()) {
			switch (event.type) {
				case EventType::KeyDown:
					routeKey(event);
					break;
				case EventType::Char:
					focus.routeCharInput(event.codepoint);
					break;
				case EventType::PointerMove:
					pointerSeen = true;
					break;
				case EventType::PointerDown:
				case EventType::PointerUp:
					pointerSeen = true;
					if (!paused) {
						sendButton(event, mods);
					}
					break;
				case EventType::Scroll:
					pointerSeen = true;
					scrollDelta = event.scroll.y;
					if (!paused && scrollDelta != 0.0F) {
						scrolled = true;
						if (sendScroll(event.position)) {
							scrollDelta = 0.0F;
						}
					}
					break;
				default:
					break;
			}
		}
		if (paused || !pointerSeen) {
			return;
		}
		const Foundation::Vec2 pointer = owner.input().frame().pointer;
		UI::InputEvent		   move = UI::InputEvent::mouseMove(pointer, mods);
		send(move);
		if (!scrolled && scrollDelta != 0.0F && sendScroll(pointer)) {
			scrollDelta = 0.0F;
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

	// A Left or Right press or release at its own position, with a move there first.
	void UiBridge::sendButton(const foundation::InputEvent& event, int mods) {
		if (event.mouseButton != MouseButton::Left && event.mouseButton != MouseButton::Right) {
			return;
		}
		UI::InputEvent move = UI::InputEvent::mouseMove(event.position, mods);
		send(move);
		UI::InputEvent edge = event.type == EventType::PointerDown ? UI::InputEvent::mouseDown(event.position, event.mouseButton, mods)
																   : UI::InputEvent::mouseUp(event.position, event.mouseButton, mods);
		send(edge);
	}

	// The pending scroll at `position`; true when something consumed it.
	bool UiBridge::sendScroll(Foundation::Vec2 position) {
		UI::InputEvent scroll = UI::InputEvent::scroll(position, scrollDelta);
		return send(scroll) || scroll.isConsumed();
	}

	bool UiBridge::send(UI::InputEvent& event) {
		UiScene* scene = topScene();
		return scene != nullptr && scene->handleInput(event);
	}

	// Every scene a worldsim app adds to the stack is a UiScene: the scene lists add UiSceneFactory
	// factories only (UiScene.h).
	UiScene* UiBridge::topScene() const {
		return static_cast<UiScene*>(owner.scenes().top()); // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast)
	}

	std::vector<const UI::IComponent*> UiBridge::uiRoots() const {
		const UiScene* scene = topScene();
		return scene != nullptr ? scene->getUiRoots() : std::vector<const UI::IComponent*>{};
	}

} // namespace engine
