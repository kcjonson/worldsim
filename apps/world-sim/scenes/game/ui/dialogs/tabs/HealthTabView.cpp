#include "HealthTabView.h"
#include "MeterDraw.h"
#include "TabStyles.h"

#include <ecs/components/Needs.h>
#include <primitives/Primitives.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>

#include <string>

namespace world_sim {

namespace {

// Need ordering (NeedType): 0 Hunger,1 Thirst,2 Energy,3 Bladder,4 Digestion,
// 5 Hygiene,6 Recreation,7 Temperature. Vitals are 0-4, comfort 5-7.
constexpr size_t kVitalCount   = 5;
constexpr size_t kComfortStart = 5;
constexpr size_t kComfortCount = 3;

constexpr float kMoodGap	= 8.0F;	 // below mood meter
constexpr float kCaptionGap = 12.0F; // below caption
constexpr float kColumnGap	= 24.0F;
constexpr float kRowGap		= 14.0F; // between meter rows
constexpr float kSectionGap = 12.0F; // between sections within a column

const char* needLabel(size_t i) {
	return (i < ecs::kNeedLabels.size()) ? ecs::kNeedLabels[i] : "Need";
}

} // namespace

void HealthTabView::create(const Foundation::Rect& bounds) {
	contentBounds = bounds;
}

void HealthTabView::update(const HealthData& data) {
	data_ = data;
}

void HealthTabView::render() {
	using namespace tabs;
	using namespace UI;

	if (!visible) return;

	const Foundation::Vec2 o	 = getContentPosition();
	const float			   width = contentBounds.width;

	// ---- Full-width Mood meter ----
	const float		  mood01 = data_.mood / 100.0F;
	const UI::Tone	  moodTone = data_.mood < 25.0F ? UI::Tone::Crit : (data_.mood < 50.0F ? UI::Tone::Warn : UI::Tone::Ok);
	const std::string moodValue = std::to_string(static_cast<int>(data_.mood)) + "% \xC2\xB7 " + (data_.moodLabel.empty() ? "Mood" : data_.moodLabel);

	float y = drawMeter(o.x, o.y, width, "MOOD", mood01, moodValue, UI::toneColor(moodTone));
	y += kMoodGap;

	drawText("Mood is computed from the needs below; it sinks as they go unmet.", {o.x, y}, fs_xs, UI::text_faint);
	y += fs_xs + kCaptionGap;

	// ---- Two columns ----
	const float colWidth = (width - kColumnGap) / 2.0F;
	const float leftX	 = o.x;
	const float rightX	 = o.x + colWidth + kColumnGap;
	const float colTop	 = y;

	// LEFT: Vital Needs + Comfort
	{
		float ly = colTop;
		ly = drawDivider(leftX, ly, colWidth, "VITAL NEEDS");
		ly += 6.0F;

		for (size_t i = 0; i < kVitalCount; ++i) {
			const UI::Tone tone = data_.isCritical[i] ? UI::Tone::Crit : (data_.needsAttention[i] ? UI::Tone::Warn : UI::Tone::Ok);
			const float	   v	= data_.needValues[i] / 100.0F;
			const std::string valueText = std::to_string(static_cast<int>(data_.needValues[i])) + "%";
			ly = drawMeter(leftX, ly, colWidth, needLabel(i), v, valueText, UI::toneColor(tone));
			ly += kRowGap;
		}

		ly += kSectionGap;
		ly = drawDivider(leftX, ly, colWidth, "COMFORT");
		ly += 6.0F;

		for (size_t k = 0; k < kComfortCount; ++k) {
			const size_t	  i			= kComfortStart + k;
			const UI::Tone	  tone		= data_.isCritical[i] ? UI::Tone::Crit : (data_.needsAttention[i] ? UI::Tone::Warn : UI::Tone::Ok);
			const float		  v			= data_.needValues[i] / 100.0F;
			const std::string valueText = std::to_string(static_cast<int>(data_.needValues[i])) + "%";
			// Comfort needs are dimmed: colonists don't act on them yet.
			ly = drawMeter(leftX, ly, colWidth, needLabel(i), v, valueText, UI::toneColor(tone), 0.7F);
			ly += kRowGap;
		}

		drawText("Comfort needs are tracked, but colonists don't act on them yet.", {leftX, ly}, fs_2xs, UI::text_faint);
	}

	// RIGHT: Body & Ailments empty state
	{
		float ry = colTop;
		ry = drawDivider(rightX, ry, colWidth, "BODY & AILMENTS");
		ry += 6.0F;

		const Foundation::Rect panel{rightX, ry, colWidth, 58.0F};
		drawEmptyState(panel, "No injuries or ailments", "Wounds, illness, and treatment arrive with the medical update.");
	}
}

} // namespace world_sim
