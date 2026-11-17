#pragma once

#include "Condition.h"
#include "TermValue.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace conditions {

	class ConditionVocabulary;

	/// A name a condition can use for a term or a symbol: letters, digits, and `_`, not starting with a
	/// digit, and not a word of the grammar (and, or, not, is, self, target).
	[[nodiscard]] bool isConditionName(std::string_view name);

	/// Turns one expression into a Condition against a frozen vocabulary. Grammar, loosest binding first:
	///
	///   expression := and-expression ("or" and-expression)*
	///   and-expression := not-expression ("and" not-expression)*
	///   not-expression := "not" not-expression | comparison
	///   comparison := operand (("<" | "<=" | ">" | ">=" | "==" | "!=") operand | "is" name)?
	///   operand := number | "-" number | "(" expression ")" | "self" | "target" | term
	///   term := name ("(" arguments? ")")?
	///
	/// Every value is typed: Truth, Number, Symbol, or Target. A term's arguments follow its signature: a
	/// Symbol parameter takes a bare name from its symbol set, a Number parameter a Number operand, and a
	/// Target parameter `self`, `target`, or a Target term. Numbers compare with any comparison, Targets
	/// with == and != only; `is` compares a Symbol term against a name from its result set.
	class ConditionParser {
	  public:
		ConditionParser(const ConditionVocabulary& vocabulary, std::string_view text);

		[[nodiscard]] ParseResult parse();

	  private:
		enum class TokenKind : uint8_t { Name, Number, Minus, LeftParenthesis, RightParenthesis, Comma, Comparison, And, Or, Not, Is, End };

		struct Token {
			TokenKind		 kind = TokenKind::End;
			std::size_t		 position = 0;
			std::string_view spelling;
			double			 number = 0.0;
			Comparison		 comparison = Comparison::Less;
		};

		struct Operand {
			uint16_t				node = 0;
			ValueType				type = ValueType::Truth;
			std::size_t				position = 0;
			std::optional<uint32_t> term; // the vocabulary slot, when the operand is a term
		};

		using OperandParser = std::optional<Operand> (ConditionParser::*)();

		[[nodiscard]] bool tokenize();

		[[nodiscard]] std::optional<Operand> parseChain(TokenKind joiner, NodeKind kind, OperandParser parseLink);
		[[nodiscard]] std::optional<Operand> parseOr();
		[[nodiscard]] std::optional<Operand> parseAnd();
		[[nodiscard]] std::optional<Operand> parseNot();
		[[nodiscard]] std::optional<Operand> parseComparison();
		[[nodiscard]] std::optional<Operand> parseIs(const Operand& left);
		[[nodiscard]] std::optional<Operand> parseOperand();
		[[nodiscard]] std::optional<Operand> parseTerm();
		[[nodiscard]] std::optional<Operand> parseArgument(uint32_t term, std::size_t parameter);

		void								  limitCount(const Operand& counted, const Operand& bound);
		[[nodiscard]] bool					  requireTruth(const Operand& operand);
		[[nodiscard]] std::optional<uint16_t> addNode(const ConditionNode& node, std::size_t position);
		[[nodiscard]] bool					  enter(std::size_t position);
		void								  leave() { --depth; }
		void								  fail(std::size_t position, std::string message);

		[[nodiscard]] const Token& peek(std::size_t ahead = 0) const;
		const Token&			   advance() { return tokens[current++]; }
		[[nodiscard]] bool		   at(TokenKind kind) const { return peek().kind == kind; }

		const ConditionVocabulary&	  vocabulary;
		std::string_view			  text;
		std::vector<Token>			  tokens;
		std::size_t					  current = 0;
		std::size_t					  depth = 0;
		Condition					  condition;
		std::optional<ConditionError> error;
	};

} // namespace conditions
