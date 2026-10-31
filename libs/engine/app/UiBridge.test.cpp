// The bridge on salal's headless platform and null backend: what the ui gets from each frame's input,
// in what order, paused or not.

#include "app/UiBridge.h"
#include "BaseShader.h"
#include "app/UiOverlay.h"
#include "app/UiScene.h"

#include <application/App.h>
#include <clipboard/IClipboard.h>
#include <focus/FocusManager.h>
#include <focus/Focusable.h>
#include <input/InputEvent.h>
#include <platform/HeadlessPlatform.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

	using foundation::Key;
	using foundation::MouseButton;
	using Type = foundation::InputEvent::Type;
	using Log = std::vector<std::string>;

	constexpr foundation::DeviceId kKeyboard{.kind = foundation::DeviceKind::Keyboard};
	constexpr foundation::DeviceId kMouse{.kind = foundation::DeviceKind::Mouse};

	foundation::InputEvent keyEvent(Key key, std::uint8_t mods = 0, Type type = Type::KeyDown, bool repeat = false) {
		return {.type = type, .device = kKeyboard, .mods = mods, .repeat = repeat, .key = key};
	}
	foundation::InputEvent character(char32_t codepoint) {
		return {.type = Type::Char, .device = kKeyboard, .codepoint = codepoint};
	}
	foundation::InputEvent pointer(Type type, float x, float y, MouseButton button = MouseButton::Left) {
		return {.type = type, .device = kMouse, .mouseButton = button, .position = {x, y}};
	}
	foundation::InputEvent wheel(float x, float y, float delta) {
		return {.type = Type::Scroll, .device = kMouse, .position = {x, y}, .scroll = {0.0F, delta}};
	}

	std::string at(Foundation::Vec2 position) {
		return std::to_string(static_cast<int>(position.x)) + "," + std::to_string(static_cast<int>(position.y));
	}

	std::string describe(const UI::InputEvent& event) {
		const std::string mods = event.modifiers != 0 ? " m" + std::to_string(event.modifiers) : "";
		const std::string button = event.button == MouseButton::Left ? "L" : event.button == MouseButton::Right ? "R" : "?";
		switch (event.type) {
			case UI::InputEvent::Type::MouseMove:
				return "move " + at(event.position) + mods;
			case UI::InputEvent::Type::MouseDown:
				return "down " + button + " " + at(event.position) + mods;
			case UI::InputEvent::Type::MouseUp:
				return "up " + button + " " + at(event.position) + mods;
			case UI::InputEvent::Type::Scroll:
				return "scroll " + std::to_string(static_cast<int>(event.scrollDelta)) + " " + at(event.position);
		}
		return "?";
	}

	class RecordingScene : public engine::UiScene {
	  public:
		struct Args {
			Log*								 log = nullptr;
			bool								 consumeScroll = true;
			std::function<void(UI::InputEvent&)> onInput;
		};

		explicit RecordingScene(Args sceneArgs)
			: args(std::move(sceneArgs)) {}

		const char* name() const override { return "recording"; }
		void		render(float /*alpha*/) override {}

		bool handleInput(UI::InputEvent& event) override {
			args.log->push_back(describe(event));
			if (args.onInput) {
				args.onInput(event);
			}
			return event.type == UI::InputEvent::Type::Scroll && args.consumeScroll;
		}

	  private:
		Args args;
	};

	class RecordingOverlay : public engine::UiOverlay {
	  public:
		RecordingOverlay(Log& overlayLog, std::string overlayName, bool consumesDowns)
			: log(overlayLog),
			  name(std::move(overlayName)),
			  consumeDowns(consumesDowns) {}

		void render() override {}

		bool handleInput(UI::InputEvent& event) override {
			log.push_back(name + " " + describe(event));
			return consumeDowns && event.type == UI::InputEvent::Type::MouseDown;
		}

	  private:
		Log&		log;
		std::string name;
		bool		consumeDowns;
	};

	// A scene with no ui, getting App's events as they are.
	class PlainScene : public app::Scene {
	  public:
		explicit PlainScene(Log& sceneLog)
			: log(sceneLog) {}

		const char* name() const override { return "plain"; }
		void		render(float /*alpha*/) override {}

		bool handleEvent(const foundation::InputEvent& event) override {
			const char* kind = event.type == Type::PointerMove	 ? "move"
							   : event.type == Type::PointerDown ? "down"
							   : event.type == Type::PointerUp	 ? "up"
							   : event.type == Type::Scroll		 ? "scroll"
																 : "?";
			log.push_back(std::string("plain ") + kind + " " + at(event.position));
			return false;
		}

	  private:
		Log& log;
	};

	class RecordingFocusable : public UI::IFocusable {
	  public:
		RecordingFocusable(Log& focusLog, std::string focusName, int tabIndex)
			: log(focusLog),
			  name(std::move(focusName)) {
			UI::FocusManager::Get().registerFocusable(this, tabIndex);
		}
		~RecordingFocusable() override { UI::FocusManager::Get().unregisterFocusable(this); }

		RecordingFocusable(const RecordingFocusable&) = delete;
		RecordingFocusable& operator=(const RecordingFocusable&) = delete;
		RecordingFocusable(RecordingFocusable&&) = delete;
		RecordingFocusable& operator=(RecordingFocusable&&) = delete;

		void onFocusGained() override { log.push_back("focus " + name); }
		void onFocusLost() override {}
		void handleKeyInput(Key key, bool shift, bool ctrl, bool alt) override {
			log.push_back(
				"key " + name + " " + std::to_string(static_cast<int>(key)) + (shift ? " shift" : "") + (ctrl ? " ctrl" : "") +
				(alt ? " alt" : "")
			);
		}
		void handleCharInput(char32_t codepoint) override { log.push_back("char " + name + " " + std::to_string(codepoint)); }
		bool canReceiveFocus() const override { return true; }

	  private:
		Log&		log;
		std::string name;
	};

	class UiBridgeTest : public ::testing::Test {
	  protected:
		void SetUp() override { start({}); }

		// A fresh App and bridge, the recording scene on top of the stack, and the log empty.
		void start(RecordingScene::Args sceneArgs) {
			stop();
			auto platform = std::make_unique<app::HeadlessPlatform>(800, 600);
			headless = platform.get();
			app = app::App::create({.applicationName = "ui-bridge-test", .baseShader = base_shader::source()}, std::move(platform));
			ASSERT_NE(app, nullptr);
			bridge = std::make_unique<engine::UiBridge>(*app);
			sceneArgs.log = &log;
			app->scenes().add("recording", [sceneArgs] { return std::make_unique<RecordingScene>(sceneArgs); });
			app->scenes().switchTo("recording");
			frame();
			log.clear();
		}

		void TearDown() override { stop(); }

		void stop() {
			if (app != nullptr) {
				app->shutdown();
			}
			bridge.reset();
			app.reset();
			headless = nullptr;
		}

		void push(const foundation::InputEvent& event) { headless->push(event); }
		void frame() { app->tick(); }

		Log								  log;
		app::HeadlessPlatform*			  headless = nullptr;
		std::unique_ptr<app::App>		  app;
		std::unique_ptr<engine::UiBridge> bridge;
	};

} // namespace

TEST_F(UiBridgeTest, KeysCharactersAndPointerEdgesKeepTheirOrder) {
	const RecordingFocusable field(log, "a", 0);
	push(keyEvent(Key::Tab));
	push(pointer(Type::PointerDown, 10, 10));
	push(character(U'h'));
	push(pointer(Type::PointerUp, 30, 30));
	frame();
	EXPECT_EQ(log, (Log{"focus a", "move 10,10", "down L 10,10", "char a 104", "move 30,30", "up L 30,30", "move 30,30"}));
}

TEST_F(UiBridgeTest, ADragInOneFrameKeepsItsPath) {
	push(pointer(Type::PointerDown, 100, 100));
	push(pointer(Type::PointerMove, 300, 300));
	push(pointer(Type::PointerUp, 300, 300));
	frame();
	EXPECT_EQ(log, (Log{"move 100,100", "down L 100,100", "move 300,300", "up L 300,300", "move 300,300"}));
}

TEST_F(UiBridgeTest, TwoClicksInOneFrameAreTwoClicks) {
	for (const float x : {10.0F, 50.0F}) {
		push(pointer(Type::PointerMove, x, 20));
		push(pointer(Type::PointerDown, x, 20));
		push(pointer(Type::PointerUp, x, 20));
	}
	frame();
	EXPECT_EQ(
		log,
		(Log{
			"move 10,20", "down L 10,20", "move 10,20", "up L 10,20", "move 50,20", "down L 50,20", "move 50,20", "up L 50,20", "move 50,20"
		})
	);
}

TEST_F(UiBridgeTest, ThreeScrollsInOneFrameAreThreeScrolls) {
	push(wheel(5, 5, -1));
	push(wheel(5, 5, -2));
	push(wheel(5, 5, -3));
	frame();
	EXPECT_EQ(log, (Log{"scroll -1 5,5", "scroll -2 5,5", "scroll -3 5,5", "move 5,5"}));
	EXPECT_EQ(bridge->consumeScroll(), 0.0F);
}

TEST_F(UiBridgeTest, AClickThenACharacterTypesIntoTheFieldTheClickFocused) {
	RecordingFocusable* field = nullptr;
	start({.onInput = [&field](UI::InputEvent& event) {
		if (event.type == UI::InputEvent::Type::MouseUp && field != nullptr) {
			UI::FocusManager::Get().setFocus(field);
		}
	}});
	RecordingFocusable input(log, "field", 0);
	field = &input;
	push(pointer(Type::PointerDown, 40, 40));
	push(pointer(Type::PointerUp, 40, 40));
	push(character(U'h'));
	frame();
	EXPECT_EQ(log, (Log{"move 40,40", "down L 40,40", "move 40,40", "up L 40,40", "focus field", "char field 104", "move 40,40"}));
}

TEST_F(UiBridgeTest, AScrollNothingTakesIsSentEveryFrameUntilConsumed) {
	start({.consumeScroll = false});
	push(wheel(5, 5, -2));
	frame();
	EXPECT_EQ(log, (Log{"scroll -2 5,5", "move 5,5"}));
	log.clear();
	frame();
	EXPECT_EQ(log, (Log{"move 5,5", "scroll -2 5,5"}));
	EXPECT_EQ(bridge->consumeScroll(), -2.0F);
	log.clear();
	frame();
	EXPECT_EQ(log, (Log{"move 5,5"}));

	// Each wheel event overwrites what's waiting.
	push(wheel(5, 5, 1));
	push(wheel(5, 5, 3));
	frame();
	EXPECT_EQ(bridge->consumeScroll(), 3.0F);
}

TEST_F(UiBridgeTest, WhilePausedKeysAndCharactersRouteAndPointerEdgesDoNot) {
	const RecordingFocusable field(log, "a", 0);
	push(pointer(Type::PointerMove, 7, 8));
	frame();
	log.clear();

	app->pause();
	push(keyEvent(Key::Tab));
	push(character(U'x'));
	push(pointer(Type::PointerDown, 7, 8));
	push(pointer(Type::PointerUp, 7, 8));
	push(wheel(7, 8, -1));
	frame();
	EXPECT_EQ(log, (Log{"focus a", "char a 120"}));
	log.clear();

	// The presses made while paused are gone; the scroll waited.
	app->resume();
	frame();
	EXPECT_EQ(log, (Log{"move 7,8", "scroll -1 7,8"}));
}

TEST_F(UiBridgeTest, NoHoverUntilAnEventCarriesAPointerPosition) {
	frame();
	EXPECT_TRUE(log.empty());
	push(keyEvent(Key::A));
	frame();
	EXPECT_TRUE(log.empty());
	push(pointer(Type::PointerMove, 3, 4));
	frame();
	frame();
	EXPECT_EQ(log, (Log{"move 3,4", "move 3,4"}));
}

TEST_F(UiBridgeTest, TabAndShiftTabMoveTheFocus) {
	const RecordingFocusable first(log, "a", 0);
	const RecordingFocusable second(log, "b", 1);
	push(keyEvent(Key::Tab));
	push(keyEvent(Key::Tab));
	push(keyEvent(Key::Tab, foundation::ModShift));
	frame();
	EXPECT_EQ(log, (Log{"focus a", "focus b", "focus a"}));
}

TEST_F(UiBridgeTest, KeysReachTheFocusedComponentWithTheirModifiers) {
	const RecordingFocusable field(log, "a", 0);
	push(keyEvent(Key::Tab));
	push(keyEvent(Key::A, foundation::ModControl));
	push(keyEvent(Key::A, foundation::ModControl, Type::KeyUp));
	push(keyEvent(Key::B, static_cast<std::uint8_t>(foundation::ModShift | foundation::ModAlt), Type::KeyDown, true));
	frame();
	const std::string a = std::to_string(static_cast<int>(Key::A));
	const std::string b = std::to_string(static_cast<int>(Key::B));
	EXPECT_EQ(log, (Log{"focus a", "key a " + a + " ctrl", "key a " + b + " shift alt"}));
}

TEST_F(UiBridgeTest, PointerModifiersComeFromTheKeysHeld) {
	push(keyEvent(Key::LeftShift));
	push(pointer(Type::PointerDown, 1, 1));
	frame();
	EXPECT_EQ(log, (Log{"move 1,1 m1", "down L 1,1 m1", "move 1,1 m1"}));
	log.clear();

	push(keyEvent(Key::LeftShift, 0, Type::KeyUp));
	push(keyEvent(Key::RightControl));
	push(keyEvent(Key::LeftAlt));
	push(keyEvent(Key::LeftSuper));
	push(pointer(Type::PointerUp, 1, 1));
	frame();
	EXPECT_EQ(log, (Log{"move 1,1 m6", "up L 1,1 m6", "move 1,1 m6"}));
}

TEST_F(UiBridgeTest, OnlyLeftAndRightButtonsReachTheUi) {
	push(pointer(Type::PointerDown, 2, 2, MouseButton::Middle));
	push(pointer(Type::PointerUp, 2, 2, MouseButton::Middle));
	push(pointer(Type::PointerDown, 2, 2, MouseButton::Right));
	frame();
	EXPECT_EQ(log, (Log{"move 2,2", "down R 2,2", "move 2,2"}));
}

TEST_F(UiBridgeTest, OverlaysGoBeforeTheSceneTheLastAddedFirst) {
	auto			   lower = std::make_unique<RecordingOverlay>(log, "lower", true);
	auto			   upper = std::make_unique<RecordingOverlay>(log, "upper", false);
	engine::UiOverlay* lowerRef = lower.get();
	engine::UiOverlay* upperRef = upper.get();
	bridge->addOverlay(*lowerRef);
	bridge->addOverlay(*upperRef);
	app->addDebugLayer(std::move(lower));
	app->addDebugLayer(std::move(upper));
	push(pointer(Type::PointerDown, 9, 9));
	frame();
	EXPECT_EQ(
		log,
		(Log{
			"upper move 9,9",
			"lower move 9,9",
			"move 9,9",
			"upper down L 9,9",
			"lower down L 9,9",
			"upper move 9,9",
			"lower move 9,9",
			"move 9,9",
		})
	);
}

TEST_F(UiBridgeTest, ASceneWithoutUiGetsTheEventsTheOverlaysLeave) {
	auto overlay = std::make_unique<RecordingOverlay>(log, "overlay", true);
	bridge->addOverlay(*overlay);
	app->addDebugLayer(std::move(overlay));
	app->scenes().add("plain", [this] { return std::make_unique<PlainScene>(log); });
	app->scenes().switchTo("plain");
	frame();
	log.clear();

	push(pointer(Type::PointerMove, 5, 5));
	push(pointer(Type::PointerDown, 5, 5));
	push(pointer(Type::PointerUp, 6, 6));
	push(wheel(6, 6, 2));
	frame();
	EXPECT_EQ(
		log,
		(Log{
			"plain move 5,5",
			"overlay move 5,5",
			"overlay down L 5,5",
			"overlay move 6,6",
			"overlay up L 6,6",
			"plain up 6,6",
			"overlay scroll 2 6,6",
			"plain scroll 6,6",
			"overlay move 6,6",
		})
	);
}

TEST_F(UiBridgeTest, TheUiGetsTheAppsClipboardAndBothSingletonsGoWithTheBridge) {
	UI::IClipboard::Get().setText("copied");
	EXPECT_EQ(app->clipboard().getText(), "copied");
	EXPECT_NO_THROW(UI::FocusManager::Get());

	app->shutdown();
	bridge.reset();
	EXPECT_THROW(UI::FocusManager::Get(), std::runtime_error);
	EXPECT_THROW(UI::IClipboard::Get(), std::runtime_error);
}
