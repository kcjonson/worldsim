// The condition grammar, its errors, and how unprovided terms evaluate, against test terms a mod would add.

#include "ConditionVocabulary.h"

#include <ecs/World.h>

#include <gtest/gtest.h>

#include <string>

using namespace conditions;

namespace {

	class ConditionVocabularyTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			colonist = world.createEntity();
			provideOrFail({.name = "yes"}, [](std::span<const TermValue>, const ConditionContext&) {
				return TermValue::fromBoolean(true);
			});
			provideOrFail({.name = "no"}, [](std::span<const TermValue>, const ConditionContext&) {
				return TermValue::fromBoolean(false);
			});
			provideOrFail({.name = "counted"}, [this](std::span<const TermValue>, const ConditionContext&) {
				++countedCalls;
				return TermValue::fromBoolean(true);
			});
			provideOrFail({.name = "level", .result = ValueType::Number}, [](std::span<const TermValue>, const ConditionContext&) {
				return TermValue::fromNumber(5.0);
			});
			provideOrFail(
				{.name = "twice", .result = ValueType::Number, .parameters = {ValueType::Number}},
				[](std::span<const TermValue> arguments, const ConditionContext&) {
					return TermValue::fromNumber(arguments[0].number * 2.0);
				}
			);
			const uint32_t rain = vocabulary.symbolId("rain");
			provideOrFail(
				{.name = "weather", .result = ValueType::Symbol, .resultSymbols = {"rain", "sun"}},
				[rain](std::span<const TermValue>, const ConditionContext&) { return TermValue::fromSymbol(rain); }
			);
		}

		void provideOrFail(TermSignature signature, TermProvider provider) {
			const std::optional<std::string> refused = vocabulary.provide(std::move(signature), std::move(provider));
			ASSERT_FALSE(refused.has_value()) << *refused;
		}

		Truth evaluate(std::string_view text) {
			const ParseResult result = vocabulary.parse(text);
			EXPECT_TRUE(result.ok()) << (result.ok() ? "" : result.error->describe());
			if (!result.ok()) {
				return Truth::Unknown;
			}
			return vocabulary.evaluate(result.condition, {.world = world, .colonist = colonist});
		}

		ConditionError errorOf(std::string_view text) {
			ParseResult result = vocabulary.parse(text);
			EXPECT_FALSE(result.ok()) << "'" << text << "' parsed";
			return result.ok() ? ConditionError{} : *result.error;
		}

		ecs::World			world;
		ecs::EntityID		colonist = ecs::kInvalidEntity;
		ConditionVocabulary vocabulary;
		int					countedCalls = 0;
	};

} // namespace

TEST_F(ConditionVocabularyTest, AndBindsTighterThanOr) {
	EXPECT_EQ(evaluate("yes or no and no"), Truth::True);
	EXPECT_EQ(evaluate("no and no or yes"), Truth::True);
	EXPECT_EQ(evaluate("no or yes and no"), Truth::False);
}

TEST_F(ConditionVocabularyTest, NotBindsTighterThanAndButLooserThanComparison) {
	EXPECT_EQ(evaluate("not no and no"), Truth::False);
	EXPECT_EQ(evaluate("not level > 3"), Truth::False);
	EXPECT_EQ(evaluate("not not yes"), Truth::True);
}

TEST_F(ConditionVocabularyTest, ParenthesesOverridePrecedence) {
	EXPECT_EQ(evaluate("(yes or no) and no"), Truth::False);
	EXPECT_EQ(evaluate("not (no or (yes and not no))"), Truth::False);
	EXPECT_EQ(evaluate("((level)) >= (5)"), Truth::True);
}

TEST_F(ConditionVocabularyTest, NestedCallsPassTheirValues) {
	EXPECT_EQ(evaluate("twice(twice(level)) == 20"), Truth::True);
	EXPECT_EQ(evaluate("twice(-2.5) < 0"), Truth::True);
}

TEST_F(ConditionVocabularyTest, EveryComparisonOperator) {
	EXPECT_EQ(evaluate("level < 6"), Truth::True);
	EXPECT_EQ(evaluate("level < 5"), Truth::False);
	EXPECT_EQ(evaluate("level <= 5"), Truth::True);
	EXPECT_EQ(evaluate("level > 4.5"), Truth::True);
	EXPECT_EQ(evaluate("level > 5"), Truth::False);
	EXPECT_EQ(evaluate("level >= 5.0"), Truth::True);
	EXPECT_EQ(evaluate("level == 5"), Truth::True);
	EXPECT_EQ(evaluate("level != 5"), Truth::False);
	EXPECT_EQ(evaluate(".5 < level"), Truth::True);
}

TEST_F(ConditionVocabularyTest, WhitespaceIsFree) {
	EXPECT_EQ(evaluate("  yes\tand\n( level>=5 )\r\n"), Truth::True);
	EXPECT_EQ(evaluate("twice( level )==10"), Truth::True);
}

TEST_F(ConditionVocabularyTest, IsComparesSymbols) {
	EXPECT_EQ(evaluate("weather is rain"), Truth::True);
	EXPECT_EQ(evaluate("weather is sun"), Truth::False);
	EXPECT_EQ(evaluate("not weather is sun"), Truth::True);
}

TEST_F(ConditionVocabularyTest, AndAndOrShortCircuit) {
	EXPECT_EQ(evaluate("no and counted"), Truth::False);
	EXPECT_EQ(evaluate("yes or counted"), Truth::True);
	EXPECT_EQ(countedCalls, 0);
	EXPECT_EQ(evaluate("yes and counted"), Truth::True);
	EXPECT_EQ(countedCalls, 1);
}

TEST_F(ConditionVocabularyTest, ParseErrorsReportTheirPosition) {
	struct Case {
		const char* text;
		std::size_t position;
	};
	const Case cases[] = {
		{"", 0},
		{"   ", 3},
		{"yes and", 7},
		{"yes and (no", 11},
		{"yes & no", 4},
		{"level = 5", 6},
		{"!yes", 0},
		{"yes no", 4},
		{"level", 0},
		{"yes > 3", 0},
		{"level > yes", 8},
		{"yes and level", 8},
		{"3abc > 1", 1},
		{"1.2.3 > 1", 0},
		{"level > -", 9},
		{"weather is", 10},
		{"weather is snow", 11},
		{"level is rain", 0},
		{"twice(yes) > 1", 6},
		{"twice() > 1", 6},
		{"twice(1, 2) > 1", 7},
		{"twice > 1", 0},
		{"near(food)", 9},
		{"near(fod, 5)", 5},
		{"near(food, colonist)", 11},
		{"near(twice(1), 5)", 5},
	};
	for (const Case& testCase : cases) {
		const ConditionError error = errorOf(testCase.text);
		EXPECT_EQ(error.position, testCase.position) << testCase.text << ": " << error.describe();
		EXPECT_EQ(error.text, testCase.text);
		EXPECT_FALSE(error.message.empty());
	}
}

TEST_F(ConditionVocabularyTest, ErrorDescriptionNamesTheColumnAndText) {
	EXPECT_EQ(errorOf("yes and frob").describe(), "unknown term 'frob' at column 9 in 'yes and frob'");
}

TEST_F(ConditionVocabularyTest, DeepNestingIsAParseErrorNotACrash) {
	const std::string deep = std::string(200, '(') + "yes" + std::string(200, ')');
	EXPECT_NE(errorOf(deep).message.find("nested too deeply"), std::string::npos);
}

TEST_F(ConditionVocabularyTest, UnknownTermsAreParseErrors) {
	EXPECT_EQ(errorOf("frobnicate").position, 0U);
	EXPECT_EQ(errorOf("yes and frob(1)").position, 8U);
	EXPECT_EQ(errorOf("dislikes(frob(bed))").position, 9U);
}

TEST_F(ConditionVocabularyTest, AnUnprovidedKnownTermParsesAndIsUnknown) {
	EXPECT_EQ(evaluate("observed"), Truth::Unknown);
	EXPECT_EQ(evaluate("not observed"), Truth::Unknown);
	EXPECT_EQ(evaluate("mood < 20"), Truth::Unknown);
	EXPECT_EQ(evaluate("scarcity(food) is short"), Truth::Unknown);
	EXPECT_EQ(evaluate("dislikes(owner(bed))"), Truth::Unknown);
	EXPECT_EQ(evaluate("observed and no"), Truth::False);
	EXPECT_EQ(evaluate("no and observed"), Truth::False);
	EXPECT_EQ(evaluate("observed or yes"), Truth::True);
	EXPECT_EQ(evaluate("observed and yes"), Truth::Unknown);
	EXPECT_EQ(evaluate("observed or no"), Truth::Unknown);
}

TEST_F(ConditionVocabularyTest, KnownTermsStillCheckTheirTypes) {
	EXPECT_EQ(errorOf("scarcity(food) is famine").position, 18U);
	EXPECT_EQ(errorOf("mood").position, 0U);
	EXPECT_EQ(errorOf("observed > 1").position, 0U);
	EXPECT_EQ(errorOf("owner_present(5)").position, 14U);
}

TEST_F(ConditionVocabularyTest, TheSpecsExamplesParse) {
	const char* examples[] = {
		"near(bed, 15) and not owner_present(bed)",
		"dislikes(owner(bed))",
		"not observed",
		"trait(volatile)",
		"count_near(colonist, 5) >= 2",
		"recent_event(owner(bed), argument, 24)",
		"time_below(12, 48)",
		"hours_worked(24) > 10 and not is_night",
		"affinity(hoard) >= 2 or holds_in_a_row > 3",
		"mood_cause is hunger and colony_mood < 30 and hour >= 22",
		"in_room and room_occupants > 3 or unexplored_in_sight",
		"current_task(work) and not mid_craft and opinion(owner(bed)) < -20",
		"attribute(sanity) > 15 and likes(owner(bed)) and need(hunger) < 30",
	};
	for (const char* example : examples) {
		const ParseResult result = vocabulary.parse(example);
		EXPECT_TRUE(result.ok()) << example << ": " << (result.ok() ? "" : result.error->describe());
	}
}

TEST_F(ConditionVocabularyTest, AModCanProvideAKnownTermLater) {
	const ParseResult before = vocabulary.parse("observed");
	ASSERT_TRUE(before.ok());
	provideOrFail({.name = "observed"}, [](std::span<const TermValue>, const ConditionContext&) { return TermValue::fromBoolean(true); });
	EXPECT_EQ(vocabulary.evaluate(before.condition, {.world = world, .colonist = colonist}), Truth::True);
}

TEST_F(ConditionVocabularyTest, ProvidingIsRefusedOnConflicts) {
	const TermProvider anything = [](std::span<const TermValue>, const ConditionContext&) {
		return TermValue::fromBoolean(true);
	};
	EXPECT_TRUE(vocabulary.provide({.name = "yes"}, anything).has_value());
	EXPECT_TRUE(vocabulary.provide({.name = "mood", .result = ValueType::Boolean}, anything).has_value());
	EXPECT_TRUE(vocabulary.provide({.name = "trait", .result = ValueType::Boolean}, anything).has_value());
	EXPECT_TRUE(vocabulary.provide({.name = "empty"}, nullptr).has_value());
	EXPECT_TRUE(vocabulary
					.provide(
						{.name = "wide",
						 .parameters = {ValueType::Number, ValueType::Number, ValueType::Number, ValueType::Number, ValueType::Number}},
						anything
					)
					.has_value());
}

TEST_F(ConditionVocabularyTest, AProviderNarrowsTheNamesAKnownTermAccepts) {
	provideOrFail(
		{.name = "trait", .parameters = {ValueType::Symbol}, .parameterSymbols = {{"volatile", "gourmand"}}},
		[](std::span<const TermValue>, const ConditionContext&) { return TermValue::fromBoolean(false); }
	);
	EXPECT_EQ(evaluate("trait(gourmand)"), Truth::False);
	EXPECT_EQ(errorOf("trait(grumpy)").position, 6U);
}
