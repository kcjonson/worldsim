// Layout Scene - live fixture for the LayoutContainer auto-layout engine.
// Exercises every engine feature: gap, padding, all six Distribution modes,
// all four CrossAlign modes, Fixed/Hug/Fill mixes (with fillWeight), nested
// containers, and wrapping Text in a stretched column. Doubles as the lint
// fixture for /api/ui/lint.

#include <GL/glew.h>

#include <components/button/Button.h>
#include <graphics/Color.h>
#include <input/InputEvent.h>
#include <layout/LayoutContainer.h>
#include <layout/LayoutTypes.h>
#include <memory>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <string>
#include <vector>
#include "SceneTypes.h"
#include <shapes/Shapes.h>
#include <utils/Log.h>

namespace {

constexpr const char* kSceneName = "layout";

class LayoutScene : public engine::IScene {
  public:
	const char* getName() const override { return kSceneName; }
	std::string exportState() override { return "{}"; }

	void onEnter() override {
		using namespace UI;
		using namespace Foundation;

		const Color slate{0.204F, 0.596F, 0.859F};
		const Color rust{0.906F, 0.298F, 0.235F};
		const Color moss{0.180F, 0.800F, 0.443F};
		const Color sand{0.945F, 0.769F, 0.059F};

		addLabel({40.0F, 20.0F}, "LayoutContainer engine fixture - distribution / crossAlign / fill / nesting / wrap", 16.0F,
				 Color::white(), "title");

		// ================================================================
		// A: every Distribution mode (vertical, gap 4)
		// ================================================================
		struct DistributionDemo {
			Distribution mode;
			const char*	 name;
		};
		const DistributionDemo distributions[] = {
			{Distribution::Start, "start"},
			{Distribution::Center, "center"},
			{Distribution::End, "end"},
			{Distribution::SpaceBetween, "between"},
			{Distribution::SpaceAround, "around"},
			{Distribution::SpaceEvenly, "evenly"},
		};
		float x = 40.0F;
		for (const auto& demo : distributions) {
			addLabel({x, 60.0F}, demo.name, 11.0F, Color::yellow());
			auto container = std::make_unique<LayoutContainer>(LayoutContainer::Args{
				.position = {x, 78.0F},
				.size = {90.0F, 150.0F},
				.direction = Direction::Vertical,
				.gap = 4.0F,
				.distribution = demo.mode,
				.id = demo.name});
			container->addChild(Rectangle(Rectangle::Args{.size = {60.0F, 24.0F}, .style = {.fill = slate}}));
			container->addChild(Rectangle(Rectangle::Args{.size = {60.0F, 24.0F}, .style = {.fill = rust}}));
			container->addChild(Rectangle(Rectangle::Args{.size = {60.0F, 24.0F}, .style = {.fill = moss}}));
			containers.push_back(std::move(container));
			x += 110.0F;
		}

		// ================================================================
		// B: every CrossAlign mode (vertical, mixed child widths)
		// ================================================================
		struct CrossAlignDemo {
			CrossAlign	mode;
			const char* name;
		};
		const CrossAlignDemo crossAligns[] = {
			{CrossAlign::Start, "cross start"},
			{CrossAlign::Center, "cross center"},
			{CrossAlign::End, "cross end"},
			{CrossAlign::Stretch, "cross stretch"},
		};
		x = 40.0F;
		for (const auto& demo : crossAligns) {
			addLabel({x, 260.0F}, demo.name, 11.0F, Color::yellow());
			auto container = std::make_unique<LayoutContainer>(LayoutContainer::Args{
				.position = {x, 278.0F},
				.size = {110.0F, 120.0F},
				.direction = Direction::Vertical,
				.gap = 4.0F,
				.crossAlign = demo.mode,
				.id = demo.name});
			const float widths[] = {40.0F, 70.0F, 55.0F};
			const Color colors[] = {slate, rust, moss};
			for (int i = 0; i < 3; i++) {
				Rectangle rect(Rectangle::Args{.size = {widths[i], 22.0F}, .style = {.fill = colors[i]}});
				rect.widthMode = SizeMode::Hug; // stretchable under CrossAlign::Stretch
				container->addChild(rect);
			}
			containers.push_back(std::move(container));
			x += 130.0F;
		}

		// ================================================================
		// C: Fixed + Button + Fill 1x/2x in a horizontal row
		// ================================================================
		addLabel({40.0F, 430.0F}, "fixed rect + button + fill 1x / fill 2x (gap 6, padding 8, cross center)", 11.0F,
				 Color::yellow());
		fillRow = std::make_unique<LayoutContainer>(LayoutContainer::Args{
			.position = {40.0F, 448.0F},
			.size = {560.0F, 52.0F},
			.direction = Direction::Horizontal,
			.gap = 6.0F,
			.padding = Insets{8.0F},
			.crossAlign = CrossAlign::Center,
			.id = "fill_row"});
		fillRow->addChild(Rectangle(Rectangle::Args{.size = {90.0F, 30.0F}, .style = {.fill = slate}, .id = "fixed_rect"}));
		fillRow->addChild(Button(Button::Args{
			.label = "Button",
			.size = {100.0F, 32.0F},
			.type = Button::Type::Primary,
			.onClick = []() { LOG_INFO(UI, "Fixture button clicked"); },
			.id = "fixture_btn"}));
		Rectangle fill1(Rectangle::Args{.size = {0.0F, 30.0F}, .style = {.fill = moss}, .id = "fill_1x"});
		fill1.widthMode = SizeMode::Fill;
		fillRow->addChild(fill1);
		Rectangle fill2(Rectangle::Args{.size = {0.0F, 30.0F}, .style = {.fill = sand}, .id = "fill_2x"});
		fill2.widthMode = SizeMode::Fill;
		fill2.fillWeight = 2.0F;
		fillRow->addChild(fill2);

		// ================================================================
		// D: vertical Fill (header + body that fills the rest)
		// ================================================================
		addLabel({700.0F, 60.0F}, "vertical fill", 11.0F, Color::yellow());
		auto verticalFill = std::make_unique<LayoutContainer>(LayoutContainer::Args{
			.position = {700.0F, 78.0F},
			.size = {120.0F, 150.0F},
			.direction = Direction::Vertical,
			.gap = 4.0F,
			.crossAlign = CrossAlign::Stretch,
			.id = "vertical_fill"});
		Rectangle header(Rectangle::Args{.size = {0.0F, 24.0F}, .style = {.fill = rust}, .id = "vf_header"});
		header.widthMode = SizeMode::Hug;
		verticalFill->addChild(header);
		Rectangle body(Rectangle::Args{.size = {0.0F, 0.0F}, .style = {.fill = slate}, .id = "vf_body"});
		body.widthMode = SizeMode::Hug;
		body.heightMode = SizeMode::Fill;
		verticalFill->addChild(body);
		containers.push_back(std::move(verticalFill));

		// ================================================================
		// E: nested container (stretched, SpaceBetween inside)
		// ================================================================
		addLabel({860.0F, 60.0F}, "nested + between", 11.0F, Color::yellow());
		auto nested = std::make_unique<LayoutContainer>(LayoutContainer::Args{
			.position = {860.0F, 78.0F},
			.size = {220.0F, 150.0F},
			.direction = Direction::Vertical,
			.gap = 6.0F,
			.padding = Insets{8.0F},
			.crossAlign = CrossAlign::Stretch,
			.id = "nested_outer"});
		Rectangle nestedHeader(Rectangle::Args{.size = {0.0F, 22.0F}, .style = {.fill = moss}, .id = "nested_header"});
		nestedHeader.widthMode = SizeMode::Hug;
		nested->addChild(nestedHeader);
		LayoutContainer innerRow(LayoutContainer::Args{
			.size = {0.0F, 0.0F}, // Hug both; the outer stretch resolves the width
			.direction = Direction::Horizontal,
			.distribution = Distribution::SpaceBetween,
			.id = "nested_inner"});
		innerRow.addChild(Rectangle(Rectangle::Args{.size = {40.0F, 26.0F}, .style = {.fill = slate}}));
		innerRow.addChild(Rectangle(Rectangle::Args{.size = {40.0F, 26.0F}, .style = {.fill = rust}}));
		innerRow.addChild(Rectangle(Rectangle::Args{.size = {40.0F, 26.0F}, .style = {.fill = sand}}));
		nested->addChild(std::move(innerRow));
		containers.push_back(std::move(nested));

		// ================================================================
		// F: wrapping Text in a stretched column (Hug height grows with it)
		// ================================================================
		addLabel({640.0F, 260.0F}, "wrap text in stretch column (hug height)", 11.0F, Color::yellow());
		auto wrapColumn = std::make_unique<LayoutContainer>(LayoutContainer::Args{
			.position = {640.0F, 278.0F},
			.size = {240.0F, 0.0F}, // Fixed width, Hug height
			.direction = Direction::Vertical,
			.gap = 8.0F,
			.padding = Insets{10.0F},
			.crossAlign = CrossAlign::Stretch,
			.id = "wrap_column"});
		wrapColumn->addChild(Text(Text::Args{
			.text = "This long paragraph has no explicit width. The stretch pass assigns the column's "
					"content width as its wrap width, the text reflows to fit, and the column's hug "
					"height grows to hold every wrapped line.",
			.style = {.color = Foundation::Color::white(), .fontSize = 13.0F, .wordWrap = true},
			.id = "wrap_text"}));
		wrapColumn->addChild(Text(Text::Args{
			.text = "hug height grew to fit",
			.style = {.color = sand, .fontSize = 11.0F},
			.id = "wrap_footer"}));
		containers.push_back(std::move(wrapColumn));

		LOG_INFO(UI, "Layout scene initialized");
	}

	void onExit() override {
		labels.clear();
		containers.clear();
		fillRow.reset();
		LOG_INFO(UI, "Layout scene exited");
	}

	bool handleInput(UI::InputEvent& event) override {
		if (fillRow && fillRow->handleEvent(event)) {
			return true;
		}
		for (auto& container : containers) {
			if (container->handleEvent(event)) {
				return true;
			}
		}
		return false;
	}

	void update(float deltaTime) override {
		if (fillRow) {
			fillRow->update(deltaTime);
		}
		for (auto& container : containers) {
			container->update(deltaTime);
		}
	}

	void render() override {
		glClearColor(0.12F, 0.12F, 0.15F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);

		for (auto& label : labels) {
			label->render();
		}
		if (fillRow) {
			fillRow->render();
		}
		for (auto& container : containers) {
			container->render();
		}
	}

	std::vector<const UI::IComponent*> getUiRoots() const override {
		std::vector<const UI::IComponent*> roots;
		for (const auto& label : labels) {
			roots.push_back(label.get());
		}
		if (fillRow) {
			roots.push_back(fillRow.get());
		}
		for (const auto& container : containers) {
			roots.push_back(container.get());
		}
		return roots;
	}

  private:
	void addLabel(Foundation::Vec2 position, const char* text, float fontSize, Foundation::Color color,
				  const char* labelId = nullptr) {
		labels.push_back(std::make_unique<UI::Text>(UI::Text::Args{
			.position = position,
			.text = text,
			.style = {.color = color, .fontSize = fontSize},
			.id = labelId}));
	}

	std::vector<std::unique_ptr<UI::Text>>			  labels;
	std::vector<std::unique_ptr<UI::LayoutContainer>> containers;
	std::unique_ptr<UI::LayoutContainer>			  fillRow;
};

} // anonymous namespace

// Export scene info for registry
namespace ui_sandbox::scenes {
extern const ui_sandbox::SceneInfo Layout = {kSceneName, []() { return std::make_unique<LayoutScene>(); }};
}
