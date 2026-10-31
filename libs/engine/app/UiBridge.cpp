#include "app/UiBridge.h"

#include "app/Keys.h"
#include "app/UiScene.h"

#include <debug/UiLayoutLint.h>
#include <debug/UiTreeSerializer.h>
#include <input/InputEvent.h>

#include <application/App.h>

#include <ranges>

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
		install();
	}

	UiBridge::~UiBridge() {
		uninstall();
	}

	// Its providers go in after App's, so they answer while it's installed.
	void UiBridge::install() {
		if (isInstalled) {
			return;
		}
		isInstalled = true;
		owner.setDispatch([this](app::App& /*app*/) { dispatch(); });
		const auto viewport = [this] {
			const Renderer::Viewport& window = owner.viewport();
			return Foundation::Vec2{static_cast<float>(window.logicalWidth), static_cast<float>(window.logicalHeight)};
		};
		uiTree = owner.devServer().stateProvider("ui.tree", [this, viewport] { return UI::serializeUiTreeJson(uiRoots(), viewport()); });
		uiLint = owner.devServer().stateProvider("ui.lint", [this, viewport] { return UI::lintUiTreeJson(uiRoots(), viewport()); });
	}

	void UiBridge::uninstall() {
		if (!isInstalled) {
			return;
		}
		isInstalled = false;
		owner.setDispatch({});
		uiTree.reset();
		uiLint.reset();
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
		const bool	paused = owner.paused();
		const int	mods = heldModifiers(owner.input().frame());
		app::Scene* plain = topScene() == nullptr ? owner.scenes().top() : nullptr;
		bool		scrolled = false;
		for (const foundation::InputEvent& event : owner.input().events()) {
			bool taken = false;
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
						taken = sendButton(event, mods);
					}
					break;
				case EventType::Scroll:
					pointerSeen = true;
					scrollDelta = event.scroll.y;
					if (!paused && scrollDelta != 0.0F) {
						scrolled = true;
						if (sendScroll(event.position)) {
							scrollDelta = 0.0F;
							taken = true;
						}
					}
					break;
				default:
					break;
			}
			if (plain != nullptr && !paused && !taken) {
				plain->handleEvent(event);
				if (event.type == EventType::Scroll) {
					scrollDelta = 0.0F;
				}
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

	// A Left or Right press or release at its own position, with a move there first; true when
	// something took the press or release.
	bool UiBridge::sendButton(const foundation::InputEvent& event, int mods) {
		if (event.mouseButton != MouseButton::Left && event.mouseButton != MouseButton::Right) {
			return false;
		}
		UI::InputEvent move = UI::InputEvent::mouseMove(event.position, mods);
		send(move);
		UI::InputEvent edge = event.type == EventType::PointerDown ? UI::InputEvent::mouseDown(event.position, event.mouseButton, mods)
																   : UI::InputEvent::mouseUp(event.position, event.mouseButton, mods);
		return send(edge);
	}

	// The pending scroll at `position`; true when something consumed it.
	bool UiBridge::sendScroll(Foundation::Vec2 position) {
		UI::InputEvent scroll = UI::InputEvent::scroll(position, scrollDelta);
		return send(scroll) || scroll.isConsumed();
	}

	bool UiBridge::send(UI::InputEvent& event) {
		for (UiOverlay* overlay : std::views::reverse(overlays)) {
			if (overlay->handleInput(event)) {
				return true;
			}
		}
		UiScene* scene = topScene();
		return scene != nullptr && scene->handleInput(event);
	}

	// Null when the top scene isn't a UiScene, or there is none.
	UiScene* UiBridge::topScene() const {
		return dynamic_cast<UiScene*>(owner.scenes().top());
	}

	std::vector<const UI::IComponent*> UiBridge::uiRoots() const {
		const UiScene* scene = topScene();
		return scene != nullptr ? scene->getUiRoots() : std::vector<const UI::IComponent*>{};
	}

} // namespace engine
