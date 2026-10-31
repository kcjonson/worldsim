#include "NavigationMenu.h"

#include <theme/Theme.h>
#include <ui/shapes/Text.h>
#include <ui/theme/Tokens.h>

#include <string_view>
#include <utility>

namespace ui_sandbox {

	namespace {
		constexpr float kMargin = 10.0F; // from the window's edges
		constexpr float kTabWidth = 116.0F;
		constexpr float kGap = 6.0F; // between the list and the tab
		constexpr float kPanelWidth = 200.0F;
		constexpr float kInset = 4.0F;

		Foundation::Vec2 logicalSize(const Renderer::Viewport& viewport) {
			return {static_cast<float>(viewport.logicalWidth), static_cast<float>(viewport.logicalHeight)};
		}
	} // namespace

	NavigationMenu::NavigationMenu(app::App& owningApp, Args args)
		: owner(owningApp),
		  scenes(std::move(args.scenes)),
		  context(logicalSize(owningApp.viewport())) {
		context.setScale(owningApp.uiScale());
		theme::apply(context);
		ui::Ref<ui::Component> root = context.root();
		root.get()->setLayout({
			.display = ui::Display::Flex,
			.flexDirection = ui::FlexDirection::Column,
			.gap = kGap,
			.padding = kMargin,
			.alignItems = ui::AlignItems::End,
			.justifyContent = ui::JustifyContent::End,
		});
		panel = root.addChild<ui::Rectangle>({
			.id = "menu_panel",
			.visible = false,
			.size = {kPanelWidth, ui::kAuto},
			.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .padding = kInset},
			.style = ui::styles::overlay,
			.focusGroup = ui::FocusGroup{.orientation = ui::FocusGroup::Orientation::Vertical, .wrap = true},
		});
		panel.addChild<ui::Text>({.margin = {6.0F, 8.0F}, .style = ui::styles::label, .text = "SCENES"});
		for (std::size_t i = 0; i < scenes.size(); ++i) {
			rows.push_back(panel.addChild<ui::ListRow>({
				.id = "menu_" + scenes[i],
				.label = scenes[i],
				.onPress = [this, i] { choose(i); },
			}));
		}
		tab = root.addChild<ui::Button>({
			.id = "menu_tab",
			.size = {kTabWidth, ui::kAuto},
			.label = "F1 scenes",
			.controlSize = ui::ControlSize::Sm,
			.onPress = [this] { setOpen(!open); },
		});
		setOpen(args.open);
	}

	bool NavigationMenu::handleEvent(const foundation::InputEvent& event) {
		using Type = foundation::InputEvent::Type;
		const bool keyboard = event.type == Type::KeyDown || event.type == Type::KeyUp || event.type == Type::Char;
		if (keyboard && event.key == foundation::Key::F1) {
			if (event.type == Type::KeyDown && !event.repeat) {
				setOpen(!open);
			}
			return true;
		}
		if (keyboard) {
			if (!open) {
				return false;
			}
			if (event.type == Type::KeyDown && event.key == foundation::Key::Escape) {
				setOpen(false);
			} else {
				context.dispatch(event);
			}
			return true;
		}
		const bool took = context.dispatch(event);
		return took && event.type != Type::FocusLost;
	}

	void NavigationMenu::update(const app::FrameContext& frame) {
		const std::size_t running = current();
		for (std::size_t i = 0; i < rows.size(); ++i) {
			if (ui::ListRow* row = rows[i].get()) {
				row->setSelected(i == running);
			}
		}
		context.update(frame.dt);
	}

	void NavigationMenu::render() {
		context.layout();
		context.render();
	}

	void NavigationMenu::onResize(const Renderer::Viewport& viewport) {
		context.setViewport(logicalSize(viewport));
	}

	void NavigationMenu::setOpen(bool value) {
		open = value;
		panel.get()->setVisible(open);
		if (open) {
			const std::size_t running = current();
			if (running < rows.size()) {
				rows[running].get()->focus();
			}
		}
	}

	void NavigationMenu::choose(std::size_t index) {
		owner.scenes().switchTo(scenes[index]);
		setOpen(false);
	}

	std::size_t NavigationMenu::current() const {
		const std::string_view top = owner.scenes().topName();
		for (std::size_t i = 0; i < scenes.size(); ++i) {
			if (scenes[i] == top) {
				return i;
			}
		}
		return scenes.size();
	}

} // namespace ui_sandbox
