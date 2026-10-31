#include "TopBar.h"

#include "scenes/shared/SalalUi.h"

#include <primitives/Primitives.h>
#include <ui/Context.h>
#include <ui/components/Button.h>
#include <ui/shapes/Circle.h>

#include <format>
#include <numbers>
#include <string>

namespace world_sim {

	namespace {
		constexpr float kPadH = 12.0F;
		constexpr float kSpeedSpacing = 2.0F;
		constexpr float kPillPadding = 4.0F;
		constexpr float kMenuWidth = 64.0F;
		constexpr float kMenuHeight = 28.0F;
		constexpr float kBellSize = 28.0F;
		constexpr float kMarkHalf = 8.0F;	// the colony mark's outer half-diagonal
		constexpr float kSubDot = 2.8F;		// the sub-line's middot
		constexpr float kFootAlpha = 0.86F; // the background's alpha at its foot
	} // namespace

	TopBar::TopBar(const Args& args)
		: onPause(args.onPause),
		  onSpeedChange(args.onSpeedChange),
		  onMenuClick(args.onMenuClick),
		  colonyName(args.colonyName) {}

	void TopBar::onMount() {
		const ui::Theme& theme = getContext()->getTheme();
		const ui::Size	 spec = getSizeSpec();
		setSize({spec.width.isAuto() ? ui::percent(100.0F) : spec.width, spec.height.isAuto() ? ui::Length(kHeight) : spec.height});
		ui::Layout row = getLayout();
		row.display = ui::Display::Flex;
		row.flexDirection = ui::FlexDirection::Row;
		row.alignItems = ui::AlignItems::Center;
		row.padding = {0.0F, kPadH};
		setLayout(row);
		setStyle(ui::Style{.backgroundColor = theme.get(tokens::color::bgBase)}.extend(getDeclaredStyle()));

		addChild<ui::Rectangle>({
			.size = {ui::kAuto, theme.get(tokens::border::bw)},
			.layout = {.position = ui::Position::Absolute, .inset = {.right = 0.0F, .bottom = 0.0F, .left = 0.0F}},
			.style = {.backgroundColor = theme.get(tokens::color::lineHairline)},
		});

		// The two sides start from nothing and grow alike, so the clock sits at the bar's centre.
		ui::Ref<ui::Component> left = addChild<ui::Component>({
			.size = {0.0F, ui::kAuto},
			.layout = {
				.display = ui::Display::Flex,
				.gap = theme.get(tokens::spacing::space3),
				.alignItems = ui::AlignItems::Center,
				.flexGrow = 1.0F
			},
		});

		// The colony mark: three squares a quarter turned, each inside the last, so they nest rather than
		// overlap.
		ui::Ref<ui::Component> mark = left.addChild<ui::Component>({.size = {kMarkHalf * 2.0F, kMarkHalf * 2.0F}});
		ui::Ref<ui::Component> inside = mark;
		float				   insideSide = kMarkHalf * 2.0F;
		const auto			   diamond = [&](float half, ui::ColorToken color, bool turned) {
			const float side = half * std::numbers::sqrt2_v<float>;
			const float offset = (insideSide - side) * 0.5F;
			inside = inside.addChild<ui::Rectangle>({
							.position = {offset, offset},
							.size = {side, side},
							.transform = {.rotate = turned ? std::numbers::pi_v<float> * 0.25F : 0.0F},
							.style = {.backgroundColor = theme.get(color)},
			});
			insideSide = side;
		};
		diamond(kMarkHalf, tokens::color::accent, true);
		diamond(kMarkHalf - 3.0F, tokens::color::bgBase, false);
		diamond(kMarkHalf - 5.5F, tokens::color::accent, false);

		ui::Ref<ui::Component> identity = left.addChild<ui::Component>({
			.layout = {
				.display = ui::Display::Flex,
				.flexDirection = ui::FlexDirection::Column,
				.gap = theme.get(tokens::spacing::space05),
				.alignItems = ui::AlignItems::Start
			},
		});
		identity.addChild<ui::Text>(
			{.id = "top_bar_colony",
			 .style = world_sim::text(
				 theme, {.family = ui::tokens::font::family::display, .size = tokens::fontSize::fsMd, .color = tokens::color::textBright}
			 ),
			 .text = colonyName}
		);
		ui::Ref<ui::Component> sub = identity.addChild<ui::Component>({
			.layout = {
				.display = ui::Display::Flex,
				.gap = theme.get(tokens::spacing::space15) - (kSubDot * 0.5F),
				.alignItems = ui::AlignItems::Center
			},
		});
		const ui::Style		   subStyle = world_sim::text(
			   theme, {.family = ui::tokens::font::family::mono, .size = tokens::fontSize::fs2xs, .color = tokens::color::textFaint}
		   );
		survivors = sub.addChild<ui::Text>({.id = "top_bar_survivors", .style = subStyle});
		// A drawn middot: the font's symbols aren't the theme's iconography.
		sub.addChild<ui::Circle>({.size = {kSubDot, kSubDot}, .style = {.backgroundColor = theme.get(tokens::color::textFaint)}});
		sol = sub.addChild<ui::Text>({.id = "top_bar_sol", .style = subStyle});

		ui::Ref<ui::Component> centre = addChild<ui::Component>({
			.layout = {.display = ui::Display::Flex, .gap = theme.get(tokens::spacing::space2), .alignItems = ui::AlignItems::Center},
		});
		day = centre.addChild<ui::Text>(
			{.id = "top_bar_day",
			 .style = world_sim::text(
				 theme, {.family = ui::tokens::font::family::display, .size = tokens::fontSize::fsMd, .color = tokens::color::textBright}
			 )}
		);
		ui::Style seasonStyle = world_sim::text(
			theme, {.family = ui::tokens::font::family::mono, .size = tokens::fontSize::fsXs, .color = tokens::color::data}
		);
		seasonStyle.letterSpacing = theme.get(tokens::fontSize::fsXs) * theme.get(tokens::letterSpacing::lsWide);
		seasonStyle.textTransform = Foundation::TextTransform::Uppercase;
		season = centre.addChild<ui::Text>({.id = "top_bar_season", .style = seasonStyle});
		clock = centre.addChild<ui::Text>(
			{.id = "top_bar_time",
			 .style = world_sim::text(
				 theme, {.family = ui::tokens::font::family::mono, .size = tokens::fontSize::fsMd, .color = tokens::color::dataBright}
			 )}
		);
		ui::Ref<ui::Rectangle> pill = centre.addChild<ui::Rectangle>({
			.id = "top_bar_speed",
			.margin = {0.0F, 0.0F, 0.0F, theme.get(tokens::spacing::space3) - theme.get(tokens::spacing::space2)},
			.layout = {.display = ui::Display::Flex, .gap = kSpeedSpacing, .padding = kPillPadding, .alignItems = ui::AlignItems::Center},
			.style = {
				.backgroundColor = theme.get(tokens::color::bgInset),
				.borderColor = theme.get(tokens::color::lineEdge),
				.borderWidth = theme.get(tokens::border::bw),
				.borderRadius = theme.get(tokens::radius::rMd)
			},
		});
		const auto			   speedButton = [&](const char* id, const char* icon, ecs::GameSpeed speed) {
			return pill.addChild<SpeedButton>({.id = id, .icon = icon, .onPress = [this, speed]() { pressSpeed(speed); }});
		};
		pauseButton = speedButton("top_bar_pause", "pause", ecs::GameSpeed::Paused);
		normalButton = speedButton("top_bar_speed1", "play", ecs::GameSpeed::Normal);
		fastButton = speedButton("top_bar_speed2", "fast_forward", ecs::GameSpeed::Fast);
		veryFastButton = speedButton("top_bar_speed3", "very_fast", ecs::GameSpeed::VeryFast);

		const float			   overhang = theme.get(tokens::spacing::space1); // the badge past the bell's right edge
		ui::Ref<ui::Component> right = addChild<ui::Component>({
			.size = {0.0F, ui::kAuto},
			.layout = {
				.display = ui::Display::Flex,
				.gap = theme.get(tokens::spacing::space3) - overhang,
				.alignItems = ui::AlignItems::Center,
				.justifyContent = ui::JustifyContent::End,
				.flexGrow = 1.0F
			},
		});
		// The bell, with room above and right for its badge over its top-right corner. No alert history
		// exists yet, so a press on it does nothing.
		const float			   rise = theme.get(tokens::spacing::space2);
		ui::Ref<ui::Component> bell =
			right.addChild<ui::Component>({.size = {kBellSize + overhang, kBellSize + (rise * 2.0F)}, .isolation = true});
		bell.addChild<ui::Button>({
			.id = "top_bar_alerts",
			.position = {0.0F, rise},
			.size = {kBellSize, kBellSize},
			.style = {.borderRadius = theme.get(tokens::radius::rSm), .color = theme.get(tokens::color::statusWarn)},
			.icon = "alert",
			.variant = ui::ButtonVariant::Ghost,
		});
		alertBadge = bell.addChild<ui::Badge>({
			.id = "top_bar_alert_count",
			.visible = false,
			.layout = {.position = ui::Position::Absolute, .inset = {.top = 0.0F, .right = 0.0F}},
			.zIndex = 1,
			.pointerEvents = ui::PointerEvents::None,
			.tone = ui::Tone::Danger,
		});
		right.addChild<ui::Button>({
			.id = "top_bar_menu",
			.size = {kMenuWidth, kMenuHeight},
			.label = "Menu",
			.variant = ui::ButtonVariant::Secondary,
			.onPress = [this]() {
				if (onMenuClick) {
					onMenuClick();
				}
			},
		});

		showSpeed(ecs::GameSpeed::Normal);
	}

	void TopBar::render() {
		Foundation::Color top = getStyle().backgroundColor.value_or(Foundation::Color::transparent());
		top.a *= opacity();
		Foundation::Color foot = top;
		foot.a *= kFootAlpha;
		Renderer::Primitives::drawRect({
			.bounds = {0.0F, 0.0F, size.x, size.y},
			.style = {.fill = top, .gradient = Foundation::LinearGradient{.from = top, .to = foot}},
			.id = getId().empty() ? nullptr : getId().c_str(),
			.key = drawKey(),
		});
	}

	void TopBar::updateData(const TimeModel& timeModel, int survivorCount) {
		if (day.get() == nullptr) {
			return;
		}
		const auto& time = timeModel.data();
		day.get()->setText(std::format("Day {}", time.day));
		season.get()->setText(time.season);
		clock.get()->setText(std::format("{:02}:{:02}", time.hour, time.minute));
		survivors.get()->setText(std::format("{} {}", survivorCount, survivorCount == 1 ? "survivor" : "survivors"));
		sol.get()->setText(std::format("Sol {}", time.day));
		showSpeed(time.speed);
	}

	void TopBar::setAlertCount(std::size_t count) {
		if (ui::Badge* badge = alertBadge.get()) {
			badge->setVisible(count > 0);
			if (count > 0) {
				badge->setLabel(std::to_string(count));
			}
		}
	}

	void TopBar::pressSpeed(ecs::GameSpeed speed) {
		if (speed == ecs::GameSpeed::Paused) {
			if (onPause) {
				onPause();
			}
		} else if (onSpeedChange) {
			onSpeedChange(speed);
		}
	}

	void TopBar::showSpeed(ecs::GameSpeed speed) {
		const auto mark = [](const ui::Ref<SpeedButton>& button, bool current) {
			if (SpeedButton* shown = button.get()) {
				shown->setCurrent(current);
			}
		};
		mark(pauseButton, speed == ecs::GameSpeed::Paused);
		mark(normalButton, speed == ecs::GameSpeed::Normal);
		mark(fastButton, speed == ecs::GameSpeed::Fast);
		mark(veryFastButton, speed == ecs::GameSpeed::VeryFast);
	}

} // namespace world_sim
