#include "ColonistListItem.h"

#include "scenes/game/ui/Look.h"

#include <ui/Context.h>
#include <ui/theme/Variants.h>

#include <algorithm>
#include <string>

namespace world_sim {

	namespace {

		constexpr float kWidth = 184.0F;
		constexpr float kHeight = 46.0F;
		constexpr float kAvatar = 30.0F;
		constexpr float kPad = 8.0F;
		constexpr float kEdge = 2.0F;

		std::string firstNameOf(const std::string& full) {
			const std::size_t space = full.find(' ');
			return space == std::string::npos ? full : full.substr(0, space);
		}

	} // namespace

	ColonistListItem::ColonistListItem(const Args& args)
		: data(args.colonist),
		  selected(args.isSelected),
		  onSelect(args.onSelect) {}

	void ColonistListItem::onMount() {
		Control::onMount();
		const ui::Theme& theme = getContext()->getTheme();
		const ui::Size	 spec = getSizeSpec();
		setSize({spec.width.isAuto() ? ui::Length(kWidth) : spec.width, spec.height.isAuto() ? ui::Length(kHeight) : spec.height});
		setLayout({
			.display = ui::Display::Flex,
			.gap = theme.get(look::t::spacing::space2),
			.padding = {0.0F, kPad},
			.alignItems = ui::AlignItems::Center,
		});
		const ui::Style edge{.backgroundColor = theme.get(look::t::color::accent)};
		selectedEdge = addChild<ui::Rectangle>({
			.size = {kEdge, ui::kAuto},
			.layout = {.position = ui::Position::Absolute, .inset = {.top = kEdge, .bottom = kEdge, .left = 0.0F}},
			.style = edge,
		});
		controlledEdge = addChild<ui::Rectangle>({
			.size = {kEdge, ui::kAuto},
			.layout = {.position = ui::Position::Absolute, .inset = {.top = kEdge, .right = 0.0F, .bottom = kEdge}},
			.style = {.backgroundColor = theme.get(look::t::color::data)},
		});
		avatar = addChild<theme::Avatar>({.size = {kAvatar, kAvatar}, .seed = data.name});
		ui::Ref<ui::Component> info = addChild<ui::Component>({
			.size = {ui::kAuto, kAvatar},
			.layout = {
				.display = ui::Display::Flex,
				.flexDirection = ui::FlexDirection::Column,
				.justifyContent = ui::JustifyContent::SpaceBetween,
				.flexGrow = 1.0F
			},
		});
		ui::Ref<ui::Component> top =
			info.addChild<ui::Component>({.layout = {.display = ui::Display::Flex, .alignItems = ui::AlignItems::Start}});
		name = top.addChild<ui::Text>(
			{.layout = {.flexGrow = 1.0F},
			 .style = look::text(theme, look::t::color::textBright, look::t::fontSize::fsSm, look::Font::Display)}
		);
		mood = top.addChild<ui::Text>({.style = look::text(theme, look::t::color::text, look::t::fontSize::fs2xs, look::Font::Mono)});
		activity = info.addChild<ui::ProgressBar>({.controlSize = ui::ControlSize::Sm, .inlineLabel = true});
		show();
	}

	void ColonistListItem::setSelected(bool value) {
		if (selected != value) {
			selected = value;
			show();
		}
	}

	void ColonistListItem::setColonistData(const adapters::ColonistData& value) {
		data = value;
		show();
	}

	void ColonistListItem::activate() {
		if (onSelect) {
			onSelect(data.id);
		}
	}

	// The card's surface, edges, avatar, labels and meter from the data, the selection and control.
	void ColonistListItem::show() {
		if (getContext() == nullptr || activity.get() == nullptr) {
			return;
		}
		const ui::Theme& theme = getContext()->getTheme();
		const bool		 controlled = data.playerControlled;
		const float		 moodRatio = std::clamp(data.mood / 100.0F, 0.0F, 1.0F);
		setStyle({
			.backgroundColor = theme.get((selected || controlled) ? look::t::color::bgPanelRaised : look::t::color::bgPanel),
			.borderColor = theme.get(controlled ? look::t::color::data : look::t::color::lineHairline),
			.borderWidth = theme.get(look::t::border::bw),
			.borderRadius = theme.get(look::t::radius::rMd),
		});
		selectedEdge.get()->setVisible(selected);
		controlledEdge.get()->setVisible(controlled);
		avatar.get()->setMood(moodRatio);
		avatar.get()->setSelected(selected);
		name.get()->setText(firstNameOf(data.name));
		ui::Style moodStyle = mood.get()->getDeclaredStyle();
		moodStyle.color = ui::toneColors(theme, ui::Tone::Auto, moodRatio).color;
		mood.get()->setStyle(moodStyle);
		mood.get()->setText(std::to_string(static_cast<int>(moodRatio * 100.0F)) + "%");
		// The task meter: an empty track while idle or still walking to the work.
		const bool		 idle = data.activity.empty();
		const bool		 acting = data.activityProgress >= 0.0F;
		ui::ProgressBar& meter = *activity.get();
		meter.setValue(acting ? data.activityProgress : 0.0F);
		meter.setTone(idle ? ui::Tone::Default : ui::Tone::Accent);
		meter.setLabel(idle ? "Idle" : data.activity);
		meter.setValueText(acting ? std::to_string(static_cast<int>(data.activityProgress * 100.0F)) + "%" : std::string());
	}

} // namespace world_sim
