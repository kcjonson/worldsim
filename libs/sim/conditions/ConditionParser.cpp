#include "ConditionParser.h"

#include "ConditionVocabulary.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>

namespace conditions {

	namespace {

		constexpr std::size_t kMaxDepth = 64;

		const char* typeName(ValueType type) {
			switch (type) {
				case ValueType::Boolean:
					return "a true-or-false condition";
				case ValueType::Number:
					return "a number";
				case ValueType::Symbol:
					return "a name";
				case ValueType::Target:
					return "a target";
			}
			return "a value";
		}

		bool isNameStart(char character) {
			return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || character == '_';
		}

		bool isDigit(char character) {
			return character >= '0' && character <= '9';
		}

		bool isNameCharacter(char character) {
			return isNameStart(character) || isDigit(character);
		}

		std::string joinNames(const std::vector<std::string>& names) {
			std::string joined;
			for (const std::string& name : names) {
				if (!joined.empty()) {
					joined += ", ";
				}
				joined += name;
			}
			return joined;
		}

		bool accepts(const std::vector<std::string>& names, std::string_view name) {
			return names.empty() || std::ranges::find(names, name) != names.end();
		}

	} // namespace

	ConditionParser::ConditionParser(ConditionVocabulary& newVocabulary, std::string_view newText)
		: vocabulary(newVocabulary),
		  text(newText) {
		condition.text = std::string(newText);
	}

	ParseResult ConditionParser::parse() {
		if (tokenize()) {
			if (at(TokenKind::End)) {
				fail(peek().position, "empty condition");
			} else if (const std::optional<Operand> root = parseOr(); root.has_value() && requireBoolean(*root)) {
				if (!at(TokenKind::End)) {
					fail(peek().position, "unexpected '" + std::string(peek().spelling) + "'");
				}
			}
		}
		if (error.has_value()) {
			return {.condition = {}, .error = std::move(error)};
		}
		return {.condition = std::move(condition), .error = std::nullopt};
	}

	bool ConditionParser::tokenize() {
		std::size_t index = 0;
		while (index < text.size()) {
			const char character = text[index];
			if (character == ' ' || character == '\t' || character == '\n' || character == '\r') {
				++index;
				continue;
			}
			Token	   token{.position = index};
			const auto twoCharacters = [&](char second) {
				return index + 1 < text.size() && text[index + 1] == second;
			};
			if (isNameStart(character)) {
				std::size_t end = index + 1;
				while (end < text.size() && isNameCharacter(text[end])) {
					++end;
				}
				token.spelling = text.substr(index, end - index);
				if (token.spelling == "and") {
					token.kind = TokenKind::And;
				} else if (token.spelling == "or") {
					token.kind = TokenKind::Or;
				} else if (token.spelling == "not") {
					token.kind = TokenKind::Not;
				} else if (token.spelling == "is") {
					token.kind = TokenKind::Is;
				} else {
					token.kind = TokenKind::Name;
				}
				index = end;
			} else if (isDigit(character) || (character == '.' && index + 1 < text.size() && isDigit(text[index + 1]))) {
				std::size_t end = index;
				while (end < text.size() && (isDigit(text[end]) || text[end] == '.')) {
					++end;
				}
				const auto [parsedEnd, result] = std::from_chars(text.data() + index, text.data() + end, token.number);
				if (result != std::errc() || parsedEnd != text.data() + end) {
					fail(index, "malformed number '" + std::string(text.substr(index, end - index)) + "'");
					return false;
				}
				if (end < text.size() && isNameStart(text[end])) {
					fail(end, "a number runs into a name");
					return false;
				}
				token.kind = TokenKind::Number;
				token.spelling = text.substr(index, end - index);
				index = end;
			} else if (character == '<' || character == '>' || character == '=' || character == '!') {
				const bool withEquals = twoCharacters('=');
				token.kind = TokenKind::Comparison;
				token.spelling = text.substr(index, withEquals ? 2 : 1);
				if (character == '<') {
					token.comparison = withEquals ? Comparison::LessOrEqual : Comparison::Less;
				} else if (character == '>') {
					token.comparison = withEquals ? Comparison::GreaterOrEqual : Comparison::Greater;
				} else if (withEquals) {
					token.comparison = character == '=' ? Comparison::Equal : Comparison::NotEqual;
				} else {
					fail(index, character == '=' ? "'=' is not a comparison; use '=='" : "'!' is not an operator; use 'not'");
					return false;
				}
				index += token.spelling.size();
			} else {
				token.spelling = text.substr(index, 1);
				switch (character) {
					case '(':
						token.kind = TokenKind::LeftParenthesis;
						break;
					case ')':
						token.kind = TokenKind::RightParenthesis;
						break;
					case ',':
						token.kind = TokenKind::Comma;
						break;
					case '-':
						token.kind = TokenKind::Minus;
						break;
					default:
						fail(index, "unexpected character '" + std::string(token.spelling) + "'");
						return false;
				}
				++index;
			}
			tokens.push_back(token);
		}
		tokens.push_back(Token{.kind = TokenKind::End, .position = text.size(), .spelling = "end of condition"});
		return true;
	}

	std::optional<ConditionParser::Operand> ConditionParser::parseOr() {
		std::optional<Operand> left = parseAnd();
		while (left.has_value() && at(TokenKind::Or)) {
			advance();
			if (!requireBoolean(*left)) {
				return std::nullopt;
			}
			const std::optional<Operand> right = parseAnd();
			if (!right.has_value() || !requireBoolean(*right)) {
				return std::nullopt;
			}
			const std::optional<uint16_t> node = addNode({.left = left->node, .right = right->node, .kind = NodeKind::Or}, left->position);
			if (!node.has_value()) {
				return std::nullopt;
			}
			left = Operand{.node = *node, .type = ValueType::Boolean, .position = left->position};
		}
		return left;
	}

	std::optional<ConditionParser::Operand> ConditionParser::parseAnd() {
		std::optional<Operand> left = parseNot();
		while (left.has_value() && at(TokenKind::And)) {
			advance();
			if (!requireBoolean(*left)) {
				return std::nullopt;
			}
			const std::optional<Operand> right = parseNot();
			if (!right.has_value() || !requireBoolean(*right)) {
				return std::nullopt;
			}
			const std::optional<uint16_t> node = addNode({.left = left->node, .right = right->node, .kind = NodeKind::And}, left->position);
			if (!node.has_value()) {
				return std::nullopt;
			}
			left = Operand{.node = *node, .type = ValueType::Boolean, .position = left->position};
		}
		return left;
	}

	std::optional<ConditionParser::Operand> ConditionParser::parseNot() {
		if (!at(TokenKind::Not)) {
			return parseComparison();
		}
		const std::size_t position = advance().position;
		if (!enter(position)) {
			return std::nullopt;
		}
		const std::optional<Operand> operand = parseNot();
		leave();
		if (!operand.has_value() || !requireBoolean(*operand)) {
			return std::nullopt;
		}
		const std::optional<uint16_t> node = addNode({.left = operand->node, .kind = NodeKind::Not}, position);
		if (!node.has_value()) {
			return std::nullopt;
		}
		return Operand{.node = *node, .type = ValueType::Boolean, .position = position};
	}

	std::optional<ConditionParser::Operand> ConditionParser::parseComparison() {
		const std::optional<Operand> left = parseOperand();
		if (!left.has_value()) {
			return std::nullopt;
		}
		if (at(TokenKind::Comparison)) {
			const Token& comparison = advance();
			if (left->type != ValueType::Number) {
				fail(left->position, "'" + std::string(comparison.spelling) + "' compares numbers, but this is " + typeName(left->type));
				return std::nullopt;
			}
			const std::optional<Operand> right = parseOperand();
			if (!right.has_value()) {
				return std::nullopt;
			}
			if (right->type != ValueType::Number) {
				fail(right->position, "'" + std::string(comparison.spelling) + "' compares numbers, but this is " + typeName(right->type));
				return std::nullopt;
			}
			const std::optional<uint16_t> node = addNode(
				{.left = left->node, .right = right->node, .kind = NodeKind::Compare, .comparison = comparison.comparison}, left->position
			);
			if (!node.has_value()) {
				return std::nullopt;
			}
			return Operand{.node = *node, .type = ValueType::Boolean, .position = left->position};
		}
		if (at(TokenKind::Is)) {
			advance();
			if (left->type != ValueType::Symbol) {
				fail(left->position, std::string("'is' compares a name, but this is ") + typeName(left->type));
				return std::nullopt;
			}
			if (!at(TokenKind::Name)) {
				fail(peek().position, "expected a name after 'is'");
				return std::nullopt;
			}
			const Token&					name = advance();
			const std::vector<std::string>& accepted = vocabulary.signature(left->term).resultSymbols;
			if (!accepts(accepted, name.spelling)) {
				fail(
					name.position,
					"'" + std::string(name.spelling) + "' is not a value of " + vocabulary.signature(left->term).name +
						" (expected one of: " + joinNames(accepted) + ")"
				);
				return std::nullopt;
			}
			const std::optional<uint16_t> node =
				addNode({.symbol = vocabulary.symbolId(name.spelling), .left = left->node, .kind = NodeKind::Is}, left->position);
			if (!node.has_value()) {
				return std::nullopt;
			}
			return Operand{.node = *node, .type = ValueType::Boolean, .position = left->position};
		}
		return left;
	}

	std::optional<ConditionParser::Operand> ConditionParser::parseOperand() {
		const Token& token = peek();
		switch (token.kind) {
			case TokenKind::Number: {
				advance();
				const std::optional<uint16_t> node = addNode({.number = token.number, .kind = NodeKind::NumberLiteral}, token.position);
				if (!node.has_value()) {
					return std::nullopt;
				}
				return Operand{.node = *node, .type = ValueType::Number, .position = token.position};
			}
			case TokenKind::Minus: {
				advance();
				if (!at(TokenKind::Number)) {
					fail(peek().position, "expected a number after '-'");
					return std::nullopt;
				}
				const Token&				  number = advance();
				const std::optional<uint16_t> node = addNode({.number = -number.number, .kind = NodeKind::NumberLiteral}, token.position);
				if (!node.has_value()) {
					return std::nullopt;
				}
				return Operand{.node = *node, .type = ValueType::Number, .position = token.position};
			}
			case TokenKind::LeftParenthesis: {
				advance();
				if (!enter(token.position)) {
					return std::nullopt;
				}
				std::optional<Operand> inner = parseOr();
				leave();
				if (!inner.has_value()) {
					return std::nullopt;
				}
				if (!at(TokenKind::RightParenthesis)) {
					fail(peek().position, "expected ')' to close the '(' at column " + std::to_string(token.position + 1));
					return std::nullopt;
				}
				advance();
				inner->position = token.position;
				return inner;
			}
			case TokenKind::Name:
				return parseTerm();
			default:
				fail(token.position, "expected a condition, a number, or '(' but found '" + std::string(token.spelling) + "'");
				return std::nullopt;
		}
	}

	std::optional<ConditionParser::Operand> ConditionParser::parseTerm() {
		const Token&				  name = advance();
		const std::optional<uint32_t> term = vocabulary.findTerm(name.spelling);
		if (!term.has_value()) {
			fail(name.position, "unknown term '" + std::string(name.spelling) + "'");
			return std::nullopt;
		}
		const TermSignature& signature = vocabulary.signature(*term);
		const std::size_t	 parameterCount = signature.parameters.size();

		std::array<uint16_t, kMaxArguments> argumentNodes{};
		if (at(TokenKind::LeftParenthesis)) {
			const std::size_t open = advance().position;
			for (std::size_t parameter = 0; parameter < parameterCount; ++parameter) {
				if (parameter > 0) {
					if (!at(TokenKind::Comma)) {
						fail(
							peek().position,
							"'" + signature.name + "' takes " + std::to_string(parameterCount) + " arguments, but this call has " +
								std::to_string(parameter)
						);
						return std::nullopt;
					}
					advance();
				}
				if (at(TokenKind::RightParenthesis)) {
					fail(
						peek().position,
						"'" + signature.name + "' takes " + std::to_string(parameterCount) + " arguments, but this call has " +
							std::to_string(parameter)
					);
					return std::nullopt;
				}
				if (!enter(peek().position)) {
					return std::nullopt;
				}
				const std::optional<Operand> argument = parseArgument(*term, parameter);
				leave();
				if (!argument.has_value()) {
					return std::nullopt;
				}
				argumentNodes[parameter] = argument->node;
			}
			if (at(TokenKind::Comma)) {
				fail(
					peek().position,
					"'" + signature.name + "' takes " + std::to_string(parameterCount) + " arguments, but this call has more"
				);
				return std::nullopt;
			}
			if (!at(TokenKind::RightParenthesis)) {
				fail(peek().position, "expected ')' to close the '(' at column " + std::to_string(open + 1));
				return std::nullopt;
			}
			advance();
		} else if (parameterCount > 0) {
			fail(name.position, "'" + signature.name + "' takes " + std::to_string(parameterCount) + " arguments");
			return std::nullopt;
		}

		if (condition.arguments.size() + parameterCount > std::numeric_limits<uint16_t>::max()) {
			fail(name.position, "condition is too long");
			return std::nullopt;
		}
		const auto firstArgument = static_cast<uint16_t>(condition.arguments.size());
		condition.arguments.insert(condition.arguments.end(), argumentNodes.begin(), argumentNodes.begin() + parameterCount);
		const std::optional<uint16_t> node = addNode(
			{.term = *term, .firstArgument = firstArgument, .argumentCount = static_cast<uint8_t>(parameterCount), .kind = NodeKind::Term},
			name.position
		);
		if (!node.has_value()) {
			return std::nullopt;
		}
		return Operand{.node = *node, .type = signature.result, .position = name.position, .term = *term};
	}

	std::optional<ConditionParser::Operand> ConditionParser::parseArgument(uint32_t term, std::size_t parameter) {
		const TermSignature& signature = vocabulary.signature(term);
		const ValueType		 expected = signature.parameters[parameter];
		const Token&		 token = peek();
		const std::string	 ordinal = "argument " + std::to_string(parameter + 1) + " of '" + signature.name + "'";

		const bool bareName = token.kind == TokenKind::Name && peek(1).kind != TokenKind::LeftParenthesis;
		if (expected == ValueType::Symbol || (expected == ValueType::Target && bareName)) {
			if (!bareName) {
				fail(token.position, ordinal + " is a name");
				return std::nullopt;
			}
			advance();
			if (expected == ValueType::Symbol) {
				static const std::vector<std::string> kAnyName;
				const std::vector<std::string>&		  accepted =
					  parameter < signature.parameterSymbols.size() ? signature.parameterSymbols[parameter] : kAnyName;
				if (!accepts(accepted, token.spelling)) {
					fail(
						token.position,
						"'" + std::string(token.spelling) + "' is not accepted as " + ordinal +
							" (expected one of: " + joinNames(accepted) + ")"
					);
					return std::nullopt;
				}
			}
			const std::optional<uint16_t> node =
				addNode({.symbol = vocabulary.symbolId(token.spelling), .kind = NodeKind::SymbolLiteral}, token.position);
			if (!node.has_value()) {
				return std::nullopt;
			}
			return Operand{.node = *node, .type = ValueType::Symbol, .position = token.position};
		}

		const std::optional<Operand> argument = parseOperand();
		if (!argument.has_value()) {
			return std::nullopt;
		}
		if (argument->type != expected) {
			fail(argument->position, ordinal + " is " + typeName(expected) + ", but this is " + typeName(argument->type));
			return std::nullopt;
		}
		return argument;
	}

	bool ConditionParser::requireBoolean(const Operand& operand) {
		if (operand.type == ValueType::Boolean) {
			return true;
		}
		fail(operand.position, std::string("expected a true-or-false condition, but this is ") + typeName(operand.type));
		return false;
	}

	std::optional<uint16_t> ConditionParser::addNode(const ConditionNode& node, std::size_t position) {
		if (condition.nodes.size() >= std::numeric_limits<uint16_t>::max()) {
			fail(position, "condition is too long");
			return std::nullopt;
		}
		condition.nodes.push_back(node);
		return static_cast<uint16_t>(condition.nodes.size() - 1);
	}

	bool ConditionParser::enter(std::size_t position) {
		if (depth >= kMaxDepth) {
			fail(position, "condition is nested too deeply");
			return false;
		}
		++depth;
		return true;
	}

	void ConditionParser::fail(std::size_t position, std::string message) {
		if (!error.has_value()) {
			error = ConditionError{.text = std::string(text), .position = position, .message = std::move(message)};
		}
	}

	const ConditionParser::Token& ConditionParser::peek(std::size_t ahead) const {
		return tokens[std::min(current + ahead, tokens.size() - 1)];
	}

} // namespace conditions
