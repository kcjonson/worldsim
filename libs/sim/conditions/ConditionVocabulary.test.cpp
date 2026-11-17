// The condition grammar, its types and errors, the build-then-freeze lifecycle, and how unprovided terms
// evaluate, against a small vocabulary declared here as a mod's data would declare it.

#include "ConditionVocabulary.h"

#include <ecs/World.h>

#include <utils/Log.h>

#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <string>

using namespace conditions;

namespace {

	using assets::ConditionParameterDef;
	using assets::ConditionTermDef;

	ConditionTermDef term(std::string name, ValueType result, std::vector<ConditionParameterDef> parameters = {}) {
		return {.name = std::move(name), .result = result, .parameters = std::move(parameters)};
	}

	ConditionParameterDef parameter(ValueType type, std::string symbols = {}) {
		return {.type = type, .symbols = std::move(symbols)};
	}

	assets::ConditionConfig testConfig() {
		using enum ValueType;
		assets::ConditionConfig config;
		config.symbolSets["weather"] = {"rain", "sun"};
		config.symbolSets["kind"] = {"food", "bed", "colonist"};
		config.terms = {
			term("yes", Truth),
			term("no", Truth),
			term("counted", Truth),
			term("level", Number),
			term("counter", Number),
			term("twice", Number, {parameter(Number)}),
			{.name = "weather", .result = Symbol, .resultSymbols = "weather"},
			term("partner", Target, {parameter(Target)}),
			term("observed", Truth),
			term("mood", Number),
			term("owner", Target, {parameter(Target)}),
			term("owner_present", Truth, {parameter(Target)}),
			term("dislikes", Truth, {parameter(Target)}),
			term("near", Truth, {parameter(Symbol, "kind"), parameter(Number)}),
		};
		return config;
	}

	struct UnprovidedLog {
		int			count = 0;
		static void sink(void* context, const foundation::LogRecord& record) {
			if (std::strstr(record.message, "which nothing provides") != nullptr) {
				++static_cast<UnprovidedLog*>(context)->count;
			}
		}
	};

	class ConditionVocabularyTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			colonist = world.createEntity();
			partnerEntity = world.createEntity();
			ASSERT_TRUE(vocabulary.declare(testConfig()).empty());

			provideOrFail("yes", ValueType::Truth, {}, [](const TermCall&) { return TermValue::fromTruth(true); });
			provideOrFail("no", ValueType::Truth, {}, [](const TermCall&) { return TermValue::fromTruth(false); });
			provideOrFail("counted", ValueType::Truth, {}, [this](const TermCall&) {
				++countedCalls;
				return TermValue::fromTruth(true);
			});
			provideOrFail("level", ValueType::Number, {}, [](const TermCall&) { return TermValue::fromNumber(5.0); });
			provideOrFail("counter", ValueType::Number, {}, [this](const TermCall& call) {
				lastCountLimit = call.countLimit;
				return TermValue::fromNumber(1.0);
			});
			provideOrFail("twice", ValueType::Number, {ValueType::Number}, [](const TermCall& call) {
				return TermValue::fromNumber(call.arguments[0].number() * 2.0);
			});
			const SymbolId rain = *vocabulary.findSymbol("rain");
			provideOrFail("weather", ValueType::Symbol, {}, [rain](const TermCall&) { return TermValue::fromSymbol(rain); });
			provideOrFail("partner", ValueType::Target, {ValueType::Target}, [this](const TermCall& call) {
				return TermValue::fromTarget(call.arguments[0].target() == colonist ? partnerEntity : colonist);
			});
			vocabulary.freeze();
		}

		void provideOrFail(std::string_view name, ValueType result, std::vector<ValueType> parameters, TermProvider provider) {
			const std::optional<std::string> refused = vocabulary.provide(name, result, std::move(parameters), std::move(provider));
			ASSERT_FALSE(refused.has_value()) << *refused;
		}

		Truth evaluate(std::string_view text, ecs::EntityID target = ecs::kInvalidEntity) {
			const ParseResult result = vocabulary.parse(text);
			EXPECT_TRUE(result.ok()) << (result.ok() ? "" : result.error->describe());
			if (!result.ok()) {
				return Truth::Unknown;
			}
			return vocabulary.evaluate(result.condition, {.world = world, .colonist = colonist, .target = target});
		}

		ConditionError errorOf(std::string_view text) {
			ParseResult result = vocabulary.parse(text);
			EXPECT_FALSE(result.ok()) << "'" << text << "' parsed";
			return result.ok() ? ConditionError{} : *result.error;
		}

		static std::string chain(const char* operand, const char* joiner, int count) {
			std::string text = operand;
			for (int index = 1; index < count; ++index) {
				text += joiner;
				text += operand;
			}
			return text;
		}

		ecs::World			world;
		ecs::EntityID		colonist = ecs::kInvalidEntity;
		ecs::EntityID		partnerEntity = ecs::kInvalidEntity;
		ConditionVocabulary vocabulary;
		int					countedCalls = 0;
		uint32_t			lastCountLimit = 0;
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

TEST_F(ConditionVocabularyTest, IsOnANumberIsAnErrorThatSaysSo) {
	const ConditionError error = errorOf("level is rain");
	EXPECT_EQ(error.position, 0U);
	EXPECT_NE(error.message.find("a number"), std::string::npos) << error.message;
	EXPECT_NE(error.message.find("=="), std::string::npos) << error.message;
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

TEST_F(ConditionVocabularyTest, UndeclaredTermsAreParseErrors) {
	EXPECT_EQ(errorOf("frobnicate").position, 0U);
	EXPECT_EQ(errorOf("yes and frob(1)").position, 8U);
	EXPECT_EQ(errorOf("dislikes(owner(frob(target)))").position, 15U);
}

TEST_F(ConditionVocabularyTest, TargetTyposAreParseErrors) {
	const ConditionError bareKind = errorOf("dislikes(owner(bed))");
	EXPECT_EQ(bareKind.position, 15U);
	EXPECT_NE(bareKind.message.find("not a target"), std::string::npos) << bareKind.message;
	EXPECT_EQ(errorOf("owner_present(slef)").position, 14U);
	EXPECT_EQ(errorOf("owner_present(5)").position, 14U);
	EXPECT_EQ(errorOf("owner_present(level)").position, 14U);
	EXPECT_EQ(errorOf("owner_present(near(food, 5))").position, 14U);
	EXPECT_EQ(errorOf("self").position, 0U);
	EXPECT_EQ(errorOf("target < self").position, 0U);
	EXPECT_EQ(errorOf("self == 5").position, 8U);
	EXPECT_EQ(errorOf("owner(target) == level").position, 17U);
	EXPECT_EQ(errorOf("near(self, 5)").position, 5U);
}

TEST_F(ConditionVocabularyTest, SelfAndABoundTargetResolve) {
	EXPECT_EQ(evaluate("target == self", colonist), Truth::True);
	EXPECT_EQ(evaluate("target != self", colonist), Truth::False);
	EXPECT_EQ(evaluate("target != self", partnerEntity), Truth::True);
	EXPECT_EQ(evaluate("partner(self) == target", partnerEntity), Truth::True);
	EXPECT_EQ(evaluate("partner(partner(self)) == self"), Truth::True);
	EXPECT_EQ(evaluate("partner(target) != self", colonist), Truth::True);
}

TEST_F(ConditionVocabularyTest, AnUnboundTargetIsUnknown) {
	EXPECT_EQ(evaluate("target == self"), Truth::Unknown);
	EXPECT_EQ(evaluate("partner(target) == self"), Truth::Unknown);
	EXPECT_EQ(evaluate("yes or target == self"), Truth::True);
}

TEST_F(ConditionVocabularyTest, ADeclaredUnprovidedTermIsUnknownAndLoggedOncePerCondition) {
	UnprovidedLog log;
	foundation::Logger::setSink(&UnprovidedLog::sink, &log);
	const ParseResult result = vocabulary.parse("observed and not observed and mood < 20");
	foundation::Logger::setSink(nullptr, nullptr);
	ASSERT_TRUE(result.ok());
#ifdef DEVELOPMENT_BUILD
	EXPECT_EQ(log.count, 1);
#endif
	EXPECT_EQ(vocabulary.evaluate(result.condition, {.world = world, .colonist = colonist}), Truth::Unknown);

	EXPECT_EQ(evaluate("not observed"), Truth::Unknown);
	EXPECT_EQ(evaluate("dislikes(owner(target))", colonist), Truth::Unknown);
	EXPECT_EQ(evaluate("owner(target) != self", colonist), Truth::Unknown);
	EXPECT_EQ(evaluate("observed and no"), Truth::False);
	EXPECT_EQ(evaluate("no and observed"), Truth::False);
	EXPECT_EQ(evaluate("observed or yes"), Truth::True);
	EXPECT_EQ(evaluate("observed and yes"), Truth::Unknown);
	EXPECT_EQ(evaluate("observed or no"), Truth::Unknown);
}

TEST_F(ConditionVocabularyTest, ALongChainIsCappedAndAShortOneEvaluates) {
	const ConditionError tooLong = errorOf(chain("yes", " and ", 30000));
	EXPECT_NE(tooLong.message.find("too long"), std::string::npos) << tooLong.message;
	EXPECT_NE(errorOf(chain("no", " or ", 30000)).message.find("too long"), std::string::npos);

	EXPECT_EQ(evaluate(chain("yes", " and ", 200)), Truth::True);
	EXPECT_EQ(evaluate(chain("no", " or ", 199) + " or yes"), Truth::True);
	EXPECT_EQ(evaluate(chain("counted", " and ", 200)), Truth::True);
	EXPECT_EQ(countedCalls, 200);
}

TEST_F(ConditionVocabularyTest, AComparisonAgainstANumberLimitsACount) {
	EXPECT_EQ(evaluate("counter >= 2"), Truth::False);
	EXPECT_EQ(lastCountLimit, 3U);
	EXPECT_EQ(evaluate("2.5 > counter"), Truth::True);
	EXPECT_EQ(lastCountLimit, 3U);
	EXPECT_EQ(evaluate("counter > -1"), Truth::True);
	EXPECT_EQ(lastCountLimit, 0U);
	EXPECT_EQ(evaluate("counter < level"), Truth::True);
	EXPECT_EQ(lastCountLimit, std::numeric_limits<uint32_t>::max());
}

TEST(ConditionVocabularyLifecycle, ParsingBeforeTheFreezeIsRefused) {
	ConditionVocabulary vocabulary;
	ASSERT_TRUE(vocabulary.declare(testConfig()).empty());
	const ParseResult early = vocabulary.parse("observed");
	ASSERT_FALSE(early.ok());
	EXPECT_NE(early.error->message.find("frozen"), std::string::npos);

	vocabulary.freeze();
	EXPECT_TRUE(vocabulary.parse("observed").ok());
}

TEST(ConditionVocabularyLifecycle, NothingIsDeclaredOrProvidedAfterTheFreeze) {
	ConditionVocabulary vocabulary;
	ASSERT_TRUE(vocabulary.declare(testConfig()).empty());
	vocabulary.freeze();
	EXPECT_TRUE(
		vocabulary.provide("observed", ValueType::Truth, {}, [](const TermCall&) { return TermValue::fromTruth(true); }).has_value()
	);
	EXPECT_FALSE(vocabulary.declare(assets::ConditionConfig{}).empty());
}

TEST(ConditionVocabularyLifecycle, ProvidingIsRefusedOnConflicts) {
	ConditionVocabulary vocabulary;
	ASSERT_TRUE(vocabulary.declare(testConfig()).empty());
	const TermProvider anything = [](const TermCall&) {
		return TermValue::fromTruth(true);
	};
	EXPECT_FALSE(vocabulary.provide("yes", ValueType::Truth, {}, anything).has_value());
	EXPECT_TRUE(vocabulary.provide("yes", ValueType::Truth, {}, anything).has_value());
	EXPECT_TRUE(vocabulary.provide("undeclared", ValueType::Truth, {}, anything).has_value());
	EXPECT_TRUE(vocabulary.provide("mood", ValueType::Truth, {}, anything).has_value());
	EXPECT_TRUE(vocabulary.provide("owner", ValueType::Target, {ValueType::Symbol}, anything).has_value());
	EXPECT_TRUE(vocabulary.provide("observed", ValueType::Truth, {}, nullptr).has_value());
}

TEST(ConditionVocabularyLifecycle, DeclaringRefusesNamesAConditionCannotUse) {
	using enum ValueType;
	assets::ConditionConfig config;
	config.symbolSets["bad"] = {"fine", "not", "2nd"};
	config.terms = {
		term("and", Truth),
		term("self", Target),
		term("target", Target),
		term("1abc", Number),
		term("has space", Number),
		term("wide", Truth, {parameter(Number), parameter(Number), parameter(Number), parameter(Number), parameter(Number)}),
		term("from_nowhere", Truth, {parameter(Symbol, "missing")}),
		term("fine", Truth),
		term("fine", Number),
	};
	ConditionVocabulary vocabulary;
	EXPECT_EQ(vocabulary.declare(config).size(), 10U);
	vocabulary.freeze();
	EXPECT_TRUE(vocabulary.parse("fine").ok());
	EXPECT_FALSE(vocabulary.parse("from_nowhere(fine)").ok());
}
