#include "debug/DebugServer.h"
#include <gtest/gtest.h>

using namespace Foundation;

// ============================================================================
// parseDevVerb: extract the /api/dev/<verb> path tail (used by the dev/test
// HTTP API to route generic DevCommands to the app).
// ============================================================================

TEST(DevCommandParse, VerbFromSimplePath) {
	EXPECT_EQ(parseDevVerb("/api/dev/freebuild"), "freebuild");
	EXPECT_EQ(parseDevVerb("/api/dev/give"), "give");
	EXPECT_EQ(parseDevVerb("/api/dev/spawn"), "spawn");
	EXPECT_EQ(parseDevVerb("/api/dev/colonist"), "colonist");
	EXPECT_EQ(parseDevVerb("/api/dev/teleport"), "teleport");
	EXPECT_EQ(parseDevVerb("/api/dev/foundation"), "foundation");
}

TEST(DevCommandParse, VerbIsLowercased) {
	EXPECT_EQ(parseDevVerb("/api/dev/FreeBuild"), "freebuild");
	EXPECT_EQ(parseDevVerb("/api/dev/SPAWN"), "spawn");
}

TEST(DevCommandParse, VerbTrimsExtraPathSegments) {
	EXPECT_EQ(parseDevVerb("/api/dev/foundation/extra"), "foundation");
	EXPECT_EQ(parseDevVerb("/api/dev/freebuild/"), "freebuild");
}

TEST(DevCommandParse, RejectsNonDevPaths) {
	EXPECT_TRUE(parseDevVerb("/api/control").empty());
	EXPECT_TRUE(parseDevVerb("/api/input").empty());
	EXPECT_TRUE(parseDevVerb("/api/dev").empty());	// no trailing verb
	EXPECT_TRUE(parseDevVerb("/api/dev/").empty()); // empty verb
	EXPECT_TRUE(parseDevVerb("").empty());
}

// ============================================================================
// DevCommand param accessors: first-match lookup with a fallback.
// ============================================================================

TEST(DevCommandParse, ParamReturnsFirstMatch) {
	DevCommand cmd;
	cmd.verb = "give";
	cmd.params = {{"n", "100"}, {"where", "site"}};

	EXPECT_EQ(cmd.param("n"), "100");
	EXPECT_EQ(cmd.param("where"), "site");
	EXPECT_TRUE(cmd.hasParam("n"));
	EXPECT_TRUE(cmd.hasParam("where"));
}

TEST(DevCommandParse, ParamFallbackWhenAbsent) {
	DevCommand cmd;
	cmd.verb = "give";
	cmd.params = {{"n", "50"}};

	EXPECT_EQ(cmd.param("missing", "default"), "default");
	EXPECT_EQ(cmd.param("missing"), ""); // empty fallback by default
	EXPECT_FALSE(cmd.hasParam("missing"));
}

TEST(DevCommandParse, ParamFirstWinsOnDuplicateKey) {
	DevCommand cmd;
	cmd.params = {{"on", "1"}, {"on", "0"}};
	EXPECT_EQ(cmd.param("on"), "1");
}
