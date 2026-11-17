// The hardest of the random inputs the guide port was compared on against the tuned
// prototype (legacy mode: common lengths and rounding off), one for each kind of decision
// the comparison found: a line and an angle just inside and just outside their catch, a
// merge at the merge distance, crossings on both sides of the minimum angle, at the radius,
// and on a shared generating node, a tie at equal distance, the previous edge's guides,
// the nearby direction limit, squaring, and the wall tool's inset corners, under both
// priority rules. Each asserts the point and every guide. Expected values are the
// prototype's outputs. Inputs that sat within a hair of a boundary were moved 1e-3 degrees
// or 1e-4 m off it, so they test the rule and not the last bits of the arithmetic. The
// corner limit never decided a random input; SnapGuides.GuideCornerLimit pins it. In the
// shared-corner tie two edges meeting at the corner nearest the cursor are exactly as near;
// the prototype breaks the tie by rounding (1e-16 m) and the port by ring order, as D12
// states, so that expectation is the port's.

#include "SnapEngine.h"
#include "SnapTestFixtures.h"

#include <assets/ConstructionRegistry.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

using namespace construction;
using namespace construction::snap_test;

namespace {

	struct ExpectedGuide {
		GuideSource	  source;
		GuideRelation relation;
		bool		  rightAngle;
		double		  fromX;
		double		  fromY;
	};

	struct HardCase {
		const char*								 name;
		bool									 wallTool;
		bool									 closest;
		DrawingAids								 aids;
		std::vector<std::vector<std::int64_t>>	 foundations; // rings, millimeters, x then y
		std::vector<std::array<std::int64_t, 4>> walls;		  // segments, millimeters, committed in order
		std::vector<Vec2>						 points;
		Vec2									 cursor;
		SnapKind								 kind;
		double									 x;
		double									 y;
		std::vector<ExpectedGuide>				 guides;
	};

	const std::vector<HardCase> kHardCases = {
		{"line just inside the catch distance (461)",
		 true,
		 false,
		 allAidsOn(),
		 {{6407, 5963, 8187, 5929, 8240, 8694, 6460, 8728}, {-971, 6014, 1279, 6617, 818, 8336, -1432, 7734}},
		 {},
		 {{-7.5289998054504395F, -4.066999912261963F}, {-7.5289998054504395F, -8.272000312805176F}},
		 {-8.777207374572754F, -4.366898059844971F},
		 SnapKind::GuideLine,
		 -8.77720737,
		 -4.06699991,
		 {{GuideSource::DrawnShape, GuideRelation::Perpendicular, true, -7.52899981, -4.06699991}}},
		{"line just outside the catch distance (569)",
		 false,
		 false,
		 DrawingAids{.angleSteps = true, .alignToExistingBuildings = true, .guidesFromWorldAxes = true},
		 {{2424, 1098, -380, 2707, -2536, -1050, 268, -2659}},
		 {{-458, -5011, 3506, -4989}, {3506, -4989, 8017, -4964}, {8017, -4964, 9776, -1460}},
		 {{7.784999847412109F, 7.8520002365112305F}, {6.8480000495910645F, 6.218999862670898F}},
		 {-2.8998119831085205F, -2.358898401260376F},
		 SnapKind::Angle,
		 -2.30892303,
		 -2.98702739,
		 {{GuideSource::DrawnShape, GuideRelation::AngleStep, false, 6.84800005, 6.21899986}}},
		{"angle just inside the catch window (4624)",
		 true,
		 false,
		 allAidsOn(),
		 {{-1546, 5665, 1867, 5893, 1945, 8800, -1550, 8272}, {-9418, -611, -5297, -706, -5259, 958, -9380, 1054}},
		 {{-149, 4844, -1871, 6566}, {5648, -2103, 6640, -168}},
		 {{-7.265999794006348F, 2.319999933242798F}, {-5.085000038146973F, 2.319999933242798F}},
		 {-4.779141426086426F, 4.643539905548096F},
		 SnapKind::GuideCrossing,
		 -5.08500004,
		 4.844,
		 {{GuideSource::DrawnShape, GuideRelation::Perpendicular, true, -5.08500004, 2.31999993},
		  {GuideSource::WorldAxes, GuideRelation::Parallel, false, -0.149, 4.844}}},
		{"angle just outside the catch window (10790)",
		 false,
		 false,
		 DrawingAids{.angleSteps = true, .guidesFromShape = true, .alignToExistingBuildings = true, .guidesFromWorldAxes = true},
		 {{5308, -1841, 9372, -1907, 9431, 1684, 5367, 1750}, {-1512, 6216, 1903, 6270, 1871, 8289, -1543, 8235}},
		 {},
		 {{-6.3979997634887695F, -6.214000225067139F},
		  {-3.930000066757202F, -4.954999923706055F},
		  {-5.89900016784668F, -1.093999981880188F},
		  {-10.446000099182129F, -3.234999895095825F},
		  {-9.029999732971191F, -6.242000102996826F}},
		 {-4.557060241699219F, -11.40502643585205F},
		 SnapKind::Angle,
		 -4.61936955,
		 -11.4583561,
		 {{GuideSource::DrawnShape, GuideRelation::AngleStep, false, -9.02999973, -6.2420001}}},
		{"merge at the merge distance (19603)",
		 true,
		 true,
		 allAidsOn(),
		 {{-598, 5678, 1589, 7326, 466, 8817, -1721, 7169}, {-922, -1906, 1149, -1868, 1084, 1707, -987, 1669}},
		 {{3153, 4928, 2004, 8020}, {2004, 8020, 690, 9331}},
		 {{-3.25600004196167F, 7.6020002365112305F}, {-0.3499999940395355F, 10.229000091552734F}},
		 {-0.1923244595527649F, 9.967263221740723F},
		 SnapKind::GuideLine,
		 -0.167146611,
		 9.98622682,
		 {{GuideSource::NearbyStructure, GuideRelation::Parallel, false, -0.349999994, 10.2290001}}},
		{"crossing just under the minimum angle (696)",
		 false,
		 false,
		 allAidsOn(),
		 {{-7599, 5237, -5564, 5782, -6474, 9176, -8508, 8631}},
		 {{2711, -1387, 1659, 1504}},
		 {{-6.093999862670898F, 11.708999633789062F},
		  {-6.951000213623047F, 14.909000396728516F},
		  {-12.03600025177002F, 13.54699993133545F}},
		 {-6.175016403198242F, 12.527124404907227F},
		 SnapKind::GuideLine,
		 -6.30386263,
		 12.4926178,
		 {{GuideSource::DrawnShape, GuideRelation::Parallel, false, -6.09399986, 11.7089996}}},
		{"crossing just over the minimum angle (2839)",
		 false,
		 false,
		 DrawingAids{.angleSteps = true, .alignToExistingBuildings = true, .guidesFromWorldAxes = true, .guideCrossings = true},
		 {{6290, -8483, 8435, -7908, 7899, -5908, 5754, -6483}},
		 {{136, -2178, 4615, -548}},
		 {{0.21899999678134918F, -4.572000026702881F},
		  {-1.409999966621399F, -4.590000152587891F},
		  {-1.340000033378601F, -10.736000061035156F}},
		 {8.15900707244873F, -15.771677017211914F},
		 SnapKind::GuideCrossing,
		 8.435,
		 -16.4847826,
		 {{GuideSource::NearbyStructure, GuideRelation::Perpendicular, true, 6.29, -8.483},
		  {GuideSource::WorldAxes, GuideRelation::Perpendicular, false, 8.435, -7.908}}},
		{"crossing at the radius (13228)",
		 true,
		 false,
		 allAidsOn(),
		 {{823, 4944, 2507, 6628, -452, 9587, -2135, 7903}, {-7272, 5507, -5654, 7248, -7532, 8742, -9014, 6956}},
		 {{-5110, 6195, -4539, 8673}},
		 {{-4.01800012588501F, 3.6649999618530273F},
		  {-7.9730000495910645F, 3.6649999618530273F},
		  {-7.9730000495910645F, 1.312999963760376F},
		  {-5.335000038146973F, 1.312999963760376F}},
		 {-0.39190712571144104F, 3.2793591022491455F},
		 SnapKind::GuideCrossing,
		 -0.451978576,
		 3.66499996,
		 {{GuideSource::DrawnShape, GuideRelation::Parallel, false, -4.01800013, 3.66499996},
		  {GuideSource::WorldAxes, GuideRelation::Perpendicular, false, -0.451978576, 9.44272879}}},
		{"crossing on a shared generating node (13156)",
		 false,
		 false,
		 DrawingAids{.alignToExistingBuildings = true, .guidesFromWorldAxes = true, .guideCrossings = true},
		 {{-1459, 5375, 1652, 5396, 1626, 9247, -1485, 9226}, {-1352, -9575, 2169, -8631, 1241, -5170, -2279, -6113}},
		 {{-453, 8932, 4358, 8932}, {4358, 8932, 4358, 7312}},
		 {{0.5659999847412109F, 0.1120000034570694F}},
		 {0.5818566679954529F, 0.13967151939868927F},
		 SnapKind::GuideLine,
		 0.560141589,
		 0.133849042,
		 {{GuideSource::NearbyStructure, GuideRelation::Parallel, false, 0.565999985, 0.112000003}}},
		{"ranking tie at equal distance (885)",
		 false,
		 false,
		 DrawingAids{.alignToExistingBuildings = true, .guidesFromWorldAxes = true, .guideCrossings = true},
		 {{1175, 940, -239, 1484, -1099, -750, 315, -1294},
		  {-5040, -8765, -5406, -5665, -9096, -5915, -9402, -8421},
		  {-7741, -2106, -5434, -775, -7049, 2021, -9355, 690}},
		 {},
		 {{1.5709999799728394F, 3.759999990463257F}, {3.1470000743865967F, 4.164000034332275F}, {2.135999917984009F, 8.109000205993652F}},
		 {3.1470000743865967F, 4.164000034332275F},
		 SnapKind::GuideLine,
		 3.14700007,
		 4.16400003,
		 {{GuideSource::WorldAxes, GuideRelation::Parallel, false, 3.14700007, 4.16400003}}},
		{"previous edge's guide closes on the first point (7304)",
		 false,
		 false,
		 DrawingAids{.angleSteps = true, .guidesFromShape = true, .alignToExistingBuildings = true, .guideCrossings = true},
		 {{-7140, -8993, -5346, -6623, -7040, -5227, -8711, -7822},
		  {-1847, -8995, 2299, -7944, 1759, -5481, -2296, -6438},
		  {-113, -2136, 1344, -1746, 390, 1812, -1066, 1422}},
		 {},
		 {{-1.5140000581741333F, -0.8870000243186951F},
		  {-1.5140000581741333F, 1.50600004196167F},
		  {1.5859999656677246F, 1.50600004196167F},
		  {1.5859999656677246F, 8.315999984741211F},
		  {-0.9819999933242798F, 8.395999908447266F}},
		 {-1.9804420471191406F, -0.47752201557159424F},
		 SnapKind::Origin,
		 -1.51400006,
		 -0.887000024,
		 {{GuideSource::DrawnShape, GuideRelation::Perpendicular, true, -0.981999993, 8.39599991}}},
		{"nearby direction limit (12768)",
		 true,
		 true,
		 DrawingAids{.guidesFromShape = true, .alignToExistingBuildings = true, .guidesFromWorldAxes = true, .guideCrossings = true},
		 {{-7482, 5279, -5406, 6843, -7190, 9211, -9266, 7647},
		  {6836, -1563, 8810, -423, 7558, 1745, 5584, 606},
		  {-1236, 5133, 2292, 7303, 971, 9886, -2609, 7850}},
		 {{7078, 7509, 5094, 9493}},
		 {{5.224999904632568F, 1.2330000400543213F}},
		 {-1.2293713092803955F, 12.671123504638672F},
		 SnapKind::GuideCrossing,
		 -1.19498006,
		 12.7922121,
		 {{GuideSource::NearbyStructure, GuideRelation::Perpendicular, true, 2.15826153, 7.34049025},
		  {GuideSource::WorldAxes, GuideRelation::Perpendicular, false, -1.19498006, 5.27798055}}},
		{"squared on an edge (14706)",
		 false,
		 false,
		 DrawingAids{.angleSteps = true, .guidesFromShape = true, .alignToExistingBuildings = true, .guidesFromWorldAxes = true},
		 {{-9081, 5658, -5479, 5593, -5423, 8702, -9025, 8768}, {-939, -8163, 989, -8162, 988, -6345, -940, -6346}},
		 {},
		 {{7.6620001792907715F, 2.0899999141693115F},
		  {7.732999801635742F, 6.026000022888184F},
		  {12.579999923706055F, 5.939000129699707F},
		  {12.696999549865723F, 12.442000389099121F},
		  {10.197999954223633F, 12.487000465393066F}},
		 {-5.442611217498779F, 6.1187310218811035F},
		 SnapKind::Edge,
		 -5.46691187,
		 6.2641069,
		 {{GuideSource::DrawnShape, GuideRelation::Perpendicular, true, 7.7329998, 6.02600002}}},
		{"shared-corner tie, ring order (13741)",
		 false,
		 false,
		 DrawingAids{.guidesFromShape = true, .alignToExistingBuildings = true, .guidesFromWorldAxes = true, .guideCrossings = true},
		 {{587, -2706, 2218, 690, -606, 2433, -2632, -914}},
		 {},
		 {},
		 {-0.909755289554596F, 2.8318228721618652F},
		 SnapKind::GuideCrossing,
		 -0.928835768,
		 2.63225735,
		 {{GuideSource::NearbyStructure, GuideRelation::Parallel, true, -0.606, 2.433},
		  {GuideSource::NearbyStructure, GuideRelation::Parallel, false, -2.632, -0.914}}},
	};

} // namespace

TEST(SnapGuidesHardCases, MatchThePrototype) {
	for (const HardCase& hard : kHardCases) {
		assets::SnappingConfig snapping;
		snapping.priorityRule = hard.closest ? assets::GuidePriorityRule::Closest : assets::GuidePriorityRule::SourceFirst;

		SnapWorld built;
		for (const auto& coordinates : hard.foundations) {
			geometry::Ring ring;
			for (std::size_t index = 0; index + 1 < coordinates.size(); index += 2) {
				ring.push_back({coordinates[index], coordinates[index + 1]});
			}
			built.addFoundation(ring);
		}
		for (const auto& wall : hard.walls) {
			built.addWall({{wall[0], wall[1]}, {wall[2], wall[3]}});
		}

		const SnapEngine engine(snapping, built.world);
		const SnapResult result = hard.wallTool ? engine.snapWall(hard.points, hard.cursor, wallInput(hard.aids))
												: engine.snap(hard.points, hard.cursor, foundationInput(hard.aids));
		EXPECT_EQ(result.kind, hard.kind) << hard.name;
		EXPECT_NEAR(result.point.x, hard.x, 1e-4) << hard.name;
		EXPECT_NEAR(result.point.y, hard.y, 1e-4) << hard.name;
		EXPECT_EQ(result.guideCount, hard.guides.size()) << hard.name;
		for (std::size_t index = 0; index < std::min(result.guideCount, hard.guides.size()); ++index) {
			const SnapGuide&	 guide = result.guides[index];
			const ExpectedGuide& expected = hard.guides[index];
			EXPECT_EQ(guide.source, expected.source) << hard.name << ", guide " << index;
			EXPECT_EQ(guide.relation, expected.relation) << hard.name << ", guide " << index;
			EXPECT_EQ(guide.rightAngle, expected.rightAngle) << hard.name << ", guide " << index;
			EXPECT_NEAR(guide.from.x, expected.fromX, 1e-4) << hard.name << ", guide " << index;
			EXPECT_NEAR(guide.from.y, expected.fromY, 1e-4) << hard.name << ", guide " << index;
		}
	}
}