#include "debug/UiTreeSerializer.h"

#include "layout/LayoutContainer.h"
#include "shapes/Shapes.h"

#include <gtest/gtest.h>

using namespace UI;
using namespace Foundation;

namespace {

	class StubComponent : public Component {
	  public:
		StubComponent(float width, float height, float componentMargin = 0.0F) {
			size = {width, height};
			margin = componentMargin;
		}

		void render() override {}
	};

} // namespace

TEST(UiTreeSerializerTest, SerializesLeafShapeFields) {
	Rectangle rect(Rectangle::Args{
		.position = {10.0F, 20.0F},
		.size = {30.0F, 40.0F},
		.id = "rect_a",
		.zIndex = 3,
		.margin = 5.0F});

	nlohmann::json node = serializeUiElement(rect);

	EXPECT_EQ(node["id"], "rect_a");
	EXPECT_EQ(node["type"], "Rectangle");
	// Args position is the content origin; the margin box starts margin earlier
	EXPECT_FLOAT_EQ(node["bounds"]["x"].get<float>(), 5.0F);
	EXPECT_FLOAT_EQ(node["bounds"]["y"].get<float>(), 15.0F);
	EXPECT_FLOAT_EQ(node["bounds"]["w"].get<float>(), 40.0F);
	EXPECT_FLOAT_EQ(node["bounds"]["h"].get<float>(), 50.0F);
	EXPECT_FLOAT_EQ(node["margin"].get<float>(), 5.0F);
	EXPECT_EQ(node["zIndex"].get<int>(), 3);
	EXPECT_TRUE(node["visible"].get<bool>());
	EXPECT_TRUE(node["children"].is_array());
	EXPECT_TRUE(node["children"].empty());
}

TEST(UiTreeSerializerTest, NullIdSerializesAsNull) {
	StubComponent stub(10.0F, 10.0F);
	nlohmann::json node = serializeUiElement(stub);

	EXPECT_TRUE(node["id"].is_null());
	EXPECT_EQ(node["type"], "Component");
}

TEST(UiTreeSerializerTest, SerializesChildrenInInsertionOrder) {
	StubComponent parent(200.0F, 200.0F);
	parent.addChild(Rectangle(Rectangle::Args{.size = {10.0F, 10.0F}, .id = "first"}));
	parent.addChild(Rectangle(Rectangle::Args{.size = {10.0F, 10.0F}, .id = "second"}));

	nlohmann::json node = serializeUiElement(parent);

	ASSERT_EQ(node["children"].size(), 2U);
	EXPECT_EQ(node["children"][0]["id"], "first");
	EXPECT_EQ(node["children"][1]["id"], "second");
}

TEST(UiTreeSerializerTest, SnapshotCarriesViewportAndRoots) {
	StubComponent rootA(50.0F, 50.0F);
	StubComponent rootB(60.0F, 60.0F);
	std::vector<const IComponent*> roots{&rootA, &rootB};

	nlohmann::json snapshot = serializeUiTree(roots, {800.0F, 600.0F});

	EXPECT_FLOAT_EQ(snapshot["viewport"]["width"].get<float>(), 800.0F);
	EXPECT_FLOAT_EQ(snapshot["viewport"]["height"].get<float>(), 600.0F);
	ASSERT_EQ(snapshot["roots"].size(), 2U);
	EXPECT_FLOAT_EQ(snapshot["roots"][1]["bounds"]["w"].get<float>(), 60.0F);
}

TEST(UiTreeSerializerTest, InvisibleElementsAreStillSerialized) {
	StubComponent stub(10.0F, 10.0F);
	stub.visible = false;

	nlohmann::json node = serializeUiElement(stub);

	EXPECT_FALSE(node["visible"].get<bool>());
}

TEST(UiTreeSerializerTest, LayoutContainerPositionsFlowIntoSnapshot) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {10.0F, 20.0F},
		.size = {200.0F, 300.0F},
		.direction = Direction::Vertical,
		.id = "stack"});
	layout.addChild(StubComponent(50.0F, 30.0F));
	layout.addChild(StubComponent(50.0F, 40.0F));
	layout.render();

	std::vector<const IComponent*> roots{&layout};
	nlohmann::json snapshot = serializeUiTree(roots, {800.0F, 600.0F});

	const nlohmann::json& node = snapshot["roots"][0];
	EXPECT_EQ(node["id"], "stack");
	EXPECT_EQ(node["type"], "LayoutContainer");
	ASSERT_EQ(node["children"].size(), 2U);
	EXPECT_FLOAT_EQ(node["children"][0]["bounds"]["y"].get<float>(), 20.0F);
	EXPECT_FLOAT_EQ(node["children"][1]["bounds"]["y"].get<float>(), 50.0F);
}
