#include "scenes/game/dev/SightJson.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

	vision::SightBody squareBody(vision::SightBody::Source source, vision::BodyKind kind, std::int64_t sideMm, std::string defName) {
		vision::SightBody body{.source = source, .kind = kind, .id = 7, .diameterMm = sideMm * 1414 / 1000, .centre = {5000, -2500}};
		const geometry::Vec2i64 corners[] = {{0, 0}, {sideMm, 0}, {sideMm, sideMm}, {0, sideMm}};
		for (std::uint32_t corner = 0; corner < 4; ++corner) {
			body.edges.push_back({corners[corner], corners[(corner + 1) % 4]});
			body.previousEdge.push_back((corner + 3) % 4);
		}
		body.defName = std::move(defName);
		return body;
	}

} // namespace

// A placed BigRock lists as opaque with its def name, serial, diameter, and centre; a wall body by its
// index and no def name; and the shadow edges index into the bodies.
TEST(SightJson, ListsEachColonistsBodiesAndShadowEdges) {
	const std::vector<vision::SightBody> bodies = {
		squareBody(vision::SightBody::Source::Placed, vision::BodyKind::Opaque, 1400, "BigRock"),
		squareBody(vision::SightBody::Source::Wall, vision::BodyKind::Soft, 400, ""),
	};
	const std::vector<vision::ShadowEdge>		   shadows = {{.body = 0, .corner = 1, .litTurn = -1}};
	geometry::SightSideValues					   ranges{};
	ranges.fill(30000);
	ranges[5] = 27400;
	const std::vector<world_sim::ColonistSightBodies> colonists = {
		{.id			= 3,
		 .name			= "Bob",
		 .eyeCentre		= {1000, 2000},
		 .leadMm		= 667,
		 .reachMm		= 30667,
		 .path			= "polygon",
		 .ranges		= &ranges,
		 .bodies		= &bodies,
		 .shadowEdges	= &shadows},
	};
	const std::string json = world_sim::sightBodiesJson(colonists);
	ASSERT_TRUE(nlohmann::json::accept(json)) << json;
	const auto bob = nlohmann::json::parse(json).at("colonists").at(0);
	EXPECT_EQ(bob.at("name"), "Bob");
	EXPECT_EQ(bob.at("leadMm"), 667);
	EXPECT_EQ(bob.at("path"), "polygon");
	ASSERT_EQ(bob.at("rangesMm").size(), geometry::kSightSides);
	EXPECT_EQ(bob.at("rangesMm").at(5), 27400);
	EXPECT_DOUBLE_EQ(bob.at("eye").at("y").get<double>(), 2.0);
	const auto rock = bob.at("bodies").at(0);
	EXPECT_EQ(rock.at("source"), "placed");
	EXPECT_EQ(rock.at("kind"), "opaque");
	EXPECT_EQ(rock.at("defName"), "BigRock");
	EXPECT_EQ(rock.at("serial"), 7);
	EXPECT_EQ(rock.at("diameterMm"), 1979);
	EXPECT_DOUBLE_EQ(rock.at("centre").at("x").get<double>(), 5.0);
	EXPECT_DOUBLE_EQ(rock.at("arrival").get<double>(), 1.0);
	const auto stub = bob.at("bodies").at(1);
	EXPECT_EQ(stub.at("source"), "wall");
	EXPECT_EQ(stub.at("kind"), "soft");
	EXPECT_EQ(stub.at("wallBody"), 7);
	EXPECT_FALSE(stub.contains("defName"));
	EXPECT_EQ(bob.at("shadowEdges").at(0).at("litTurn"), -1);
}

// Every tunable lists with its value, bounds, and start value; a name sets its field, and an unknown
// name sets nothing.
TEST(SightJson, TunablesListWithTheirRangesAndSetByName) {
	vision::SightTunables tunables;
	EXPECT_TRUE(world_sim::setSightTunable(tunables, "placedOpaqueLeastDiameterMm", 1800));
	EXPECT_FALSE(world_sim::setSightTunable(tunables, "noSuchTunable", 1));
	const std::string json = world_sim::sightTunablesJson(tunables);
	ASSERT_TRUE(nlohmann::json::accept(json)) << json;
	const auto listed = nlohmann::json::parse(json).at("tunables");
	ASSERT_EQ(listed.size(), vision::sightTunableRanges().size());
	bool found = false;
	for (const auto& tunable : listed) {
		if (tunable.at("name") == "placedOpaqueLeastDiameterMm") {
			found = true;
			EXPECT_EQ(tunable.at("value"), 1800);
			EXPECT_EQ(tunable.at("start"), 1400);
			EXPECT_EQ(tunable.at("least"), 100);
			EXPECT_EQ(tunable.at("most"), 2000);
		}
	}
	EXPECT_TRUE(found);
}
