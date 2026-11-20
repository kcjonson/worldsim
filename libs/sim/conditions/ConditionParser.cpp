#include "ConditionParser.h"

#include "ConditionVocabulary.h"

#include <assets/ConditionConfig.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>

namespace conditions {

	namespace {

		constexpr std::size_t kMaxDepth = 64;

		const char* typeName(ValueType type) {
			switch (type) {
				case ValueType::Truth:
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

		std::string joinNames(std::span<const DeclaredSymbol> symbols) {
			std::string joined;
			for (const DeclaredSymbol& symbol : symbols) {
				if (!joined.empty()) {
					joined += ", ";
				}
				joined += symbol.name;
			}
			return joined;
		}

		std::string expectedNames(const ConditionVocabulary& vocabulary, uint32_t set) {
			if (vocabulary.isOpenSet(set)) {
				return "expected a name";
			}
			const std::span<const DeclaredSymbol> names = vocabulary.symbolSet(set);
			return names.empty() ? "no \"" + vocabulary.symbolSetName(set) + "\" names are declared" : "expected one of: " + joinNames(names);
		}

		/// The first count past `bound`: every count from there compares against `bound` the same way.
		uint32_t countPast(double bound) {
			if (bound < 0.0) {
				return 0;
			}
			const double past = std::floor(bound) + 1.0;
			return past >= static_cast<double>(std::numeric_limits<uint32_t>::max()) ? std::numeric_limits<uint32_t>::max()
																					 : static_cast<uint32_t>(past);
		}

	} // namespace

	bool isConditionName(std::string_view name) {
		if (name.empty() || !isNameStart(name.front()) || !std::ranges::all_of(name, isNameCharacter)) {
			return false;
		}
		return std::ranges::find(assets::kConditionReservedWords, name) == assets::kConditionReservedWords.end();
	}

	ConditionParser::ConditionParser(const ConditionVocabulary& newVocabulary, std::string_view newText)
		: vocabulary(newVocabulary),
		  text(newText) {
		condition.text = std::string(newText);
	}

	ParseResult ConditionParser::parse() {
		if (tokenize()) {
			if (at(TokenKind::End)) {
				fail(peek().position, "empty condition");
			} else if (const std::optional<Operand> root = parseOr(); root.has_value() && requireTruth(*root)) {
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
				if (token.spelling == assets::kConditionAnd) {
					token.kind = TokenKind::And;
				} else if (token.spelling == assets::kConditionOr) {
					token.kind = TokenKind::Or;
				} else if (token.spelling == assets::kConditionNot) {
					token.kind = TokenKind::Not;
				} else if (token.spelling == assets::kConditionIs) {
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

	std::optional<ConditionParser::Operand> ConditionParser::parseChain(TokenKind joiner, NodeKind kind, OperandParser parseLink) {
		std::optional<Operand> first = (this->*parseLink)();
		if (!first.has_value() || !at(joiner)) {
			return first;
		}
		if (!requireTruth(*first)) {
			return std::nullopt;
		}
		std::vector<uint16_t> links{first->node};
		while (at(joiner)) {
			advance();
			const std::optional<Operand> next = (this->*parseLink)();
			if (!next.has_value() || !requireTruth(*next)) {
				return std::nullopt;
			}
			links.push_back(next->node);
		}
		const auto firstChild = static_cast<uint16_t>(condition.children.size());
		condition.children.insert(condition.children.end(), links.begin(), links.end());
		const std::optional<uint16_t> node =
			addNode({.firstChild = firstChild, .childCount = static_cast<uint16_t>(links.size()), .kind = kind}, first->position);
		if (!node.has_value()) {
			return std::nullopt;
		}
		return Operand{.node = *node, .type = ValueType::Truth, .position = first->position};
	}

	std::optional<ConditionParser::Operand> ConditionParser::parseOr() {
		return parseChain(TokenKind::Or, NodeKind::Or, &ConditionParser::parseAnd);
	}

	std::optional<ConditionParser::Operand> ConditionParser::parseAnd() {
		return parseChain(TokenKind::And, NodeKind::And, &ConditionParser::parseNot);
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
		if (!operand.has_value() || !requireTruth(*operand)) {
			return std::nullopt;
		}
		const std::optional<uint16_t> node = addNode({.left = operand->node, .kind = NodeKind::Not}, position);
		if (!node.has_value()) {
			return std::nullopt;
		}
		return Operand{.node = *node, .type = ValueType::Truth, .position = position};
	}

	std::optional<ConditionParser::Operand> ConditionParser::parseComparison() {
		const std::optional<Operand> left = parseOperand();
		if (!left.has_value()) {
			return std::nullopt;
		}
		if (at(TokenKind::Is)) {
			return parseIs(*left);
		}
		if (!at(TokenKind::Comparison)) {
			return left;
		}

		const Token&	  comparison = advance();
		const std::string spelling = "'" + std::string(comparison.spelling) + "'";
		const bool		  equality = comparison.comparison == Comparison::Equal || comparison.comparison == Comparison::NotEqual;
		if (left->type == ValueType::Target && !equality) {
			fail(left->position, spelling + " compares numbers; targets compare only with == and !=");
			return std::nullopt;
		}
		if (left->type != ValueType::Number && left->type != ValueType::Target) {
			fail(left->position, spelling + " compares numbers or targets, but this is " + typeName(left->type));
			return std::nullopt;
		}
		const std::optional<Operand> right = parseOperand();
		if (!right.has_value()) {
			return std::nullopt;
		}
		if (right->type != left->type) {
			fail(
				right->position,
				spelling + " compares " + typeName(left->type) + " with " + typeName(left->type) + ", but this is " + typeName(right->type)
			);
			return std::nullopt;
		}
		if (left->type == ValueType::Number) {
			limitCount(*left, *right);
			limitCount(*right, *left);
		}
		const std::optional<uint16_t> node = addNode(
			{.left = left->node, .right = right->node, .kind = NodeKind::Compare, .comparison = comparison.comparison}, left->position
		);
		if (!node.has_value()) {
			return std::nullopt;
		}
		return Operand{.node = *node, .type = ValueType::Truth, .position = left->position};
	}

	std::optional<ConditionParser::Operand> ConditionParser::parseIs(const Operand& left) {
		advance();
		if (left.type != ValueType::Symbol || !left.term.has_value()) {
			fail(
				left.position,
				std::string("'is' compares a term that answers with a name, but this is ") + typeName(left.type) +
					(left.type == ValueType::Number ? "; compare numbers with ==, <, or >" : "")
			);
			return std::nullopt;
		}
		if (!at(TokenKind::Name)) {
			fail(peek().position, "expected a name after 'is'");
			return std::nullopt;
		}
		const Token&		 name = advance();
		const TermSignature& signature = vocabulary.signature(*left.term);
		const uint32_t		 set = *signature.resultSymbolSet;
		const auto			 symbol = findName(set, name.spelling);
		if (!symbol.has_value()) {
			fail(
				name.position,
				"'" + std::string(name.spelling) + "' is not a value of " + signature.name + " (" +
					expectedNames(vocabulary, set) + ")"
			);
			return std::nullopt;
		}
		if (*symbol == kUnlistedSymbol) {
			noteUnanswered(*left.term, name.spelling);
		}
		const std::optional<uint16_t> node = addNode({.symbol = *symbol, .left = left.node, .kind = NodeKind::Is}, left.position);
		if (!node.has_value()) {
			return std::nullopt;
		}
		return Operand{.node = *node, .type = ValueType::Truth, .position = left.position};
	}

	std::optional<ConditionParser::Operand> ConditionParser::parseOperand() {
		const Token& token = peek();
		switch (token.kind) {
			case TokenKind::Number:
			case TokenKind::Minus: {
				advance();
				double number = token.number;
				if (token.kind == TokenKind::Minus) {
					if (!at(TokenKind::Number)) {
						fail(peek().position, "expected a number after '-'");
						return std::nullopt;
					}
					number = -advance().number;
				}
				const std::optional<uint16_t> node = addNode({.number = number, .kind = NodeKind::NumberLiteral}, token.position);
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
			case TokenKind::Name: {
				if (token.spelling != assets::kConditionSelf && token.spelling != assets::kConditionTarget) {
					return parseTerm();
				}
				advance();
				const std::optional<uint16_t> node =
					addNode({.kind = token.spelling == assets::kConditionSelf ? NodeKind::Self : NodeKind::BoundTarget}, token.position);
				if (!node.has_value()) {
					return std::nullopt;
				}
				return Operand{.node = *node, .type = ValueType::Target, .position = token.position};
			}
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
		const auto			 countMismatch = [&](std::size_t given) {
			  return "'" + signature.name + "' takes " + std::to_string(parameterCount) + " arguments, but this call has " +
					 (given > parameterCount ? std::string("more") : std::to_string(given));
		};

		std::array<uint16_t, kMaxArguments> argumentNodes{};
		if (at(TokenKind::LeftParenthesis)) {
			const std::size_t open = advance().position;
			for (std::size_t parameter = 0; parameter < parameterCount; ++parameter) {
				if (parameter > 0) {
					if (!at(TokenKind::Comma)) {
						fail(peek().position, countMismatch(parameter));
						return std::nullopt;
					}
					advance();
				}
				if (at(TokenKind::RightParenthesis)) {
					fail(peek().position, countMismatch(parameter));
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
				fail(peek().position, countMismatch(parameterCount + 1));
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

		const auto firstChild = static_cast<uint16_t>(condition.children.size());
		condition.children.insert(condition.children.end(), argumentNodes.begin(), argumentNodes.begin() + parameterCount);
		const std::optional<uint16_t> node = addNode(
			{.term = *term, .firstChild = firstChild, .childCount = static_cast<uint16_t>(parameterCount), .kind = NodeKind::Term},
			name.position
		);
		if (!node.has_value()) {
			return std::nullopt;
		}
		return Operand{.node = *node, .type = signature.result, .position = name.position, .term = *term};
	}

	std::optional<ConditionParser::Operand> ConditionParser::parseArgument(uint32_t term, std::size_t parameter) {
		const TermSignature& signature = vocabulary.signature(term);
		const TermParameter& expected = signature.parameters[parameter];
		const Token&		 token = peek();
		const std::string	 ordinal = "argument " + std::to_string(parameter + 1) + " of '" + signature.name + "'";
		const bool			 bareName = token.kind == TokenKind::Name && peek(1).kind != TokenKind::LeftParenthesis;

		if (expected.type == ValueType::Symbol) {
			const uint32_t set = *expected.symbolSet;
			if (!bareName) {
				fail(token.position, ordinal + " needs a name (" + expectedNames(vocabulary, set) + ")");
				return std::nullopt;
			}
			advance();
			const auto symbol = findName(set, token.spelling);
			if (!symbol.has_value()) {
				fail(
					token.position,
					"'" + std::string(token.spelling) + "' is not accepted as " + ordinal + " (" + expectedNames(vocabulary, set) +
						")"
				);
				return std::nullopt;
			}
			if (*symbol == kUnlistedSymbol || vocabulary.isUnbound(term, parameter, *symbol)) {
				noteUnanswered(term, token.spelling);
			}
			const std::optional<uint16_t> node = addNode({.symbol = *symbol, .kind = NodeKind::SymbolLiteral}, token.position);
			if (!node.has_value()) {
				return std::nullopt;
			}
			return Operand{.node = *node, .type = ValueType::Symbol, .position = token.position};
		}

		if (expected.type == ValueType::Target && bareName && token.spelling != assets::kConditionSelf &&
			token.spelling != assets::kConditionTarget && !vocabulary.findTerm(token.spelling).has_value()) {
			fail(
				token.position,
				"'" + std::string(token.spelling) + "' is not a target; " + ordinal + " is self, target, or a term that names one"
			);
			return std::nullopt;
		}

		const std::optional<Operand> argument = parseOperand();
		if (!argument.has_value()) {
			return std::nullopt;
		}
		if (argument->type != expected.type) {
			fail(argument->position, ordinal + " is " + typeName(expected.type) + ", but this is " + typeName(argument->type));
			return std::nullopt;
		}
		return argument;
	}

	std::optional<SymbolId> ConditionParser::findName(uint32_t set, std::string_view name) const {
		const std::span<const DeclaredSymbol> listed = vocabulary.symbolSet(set);
		const auto							  found = std::ranges::find(listed, name, &DeclaredSymbol::name);
		if (found != listed.end()) {
			return found->id;
		}
		if (vocabulary.isOpenSet(set) && isConditionName(name)) {
			return kUnlistedSymbol;
		}
		return std::nullopt;
	}

	void ConditionParser::noteUnanswered(uint32_t term, std::string_view name) {
		if (!vocabulary.isProvided(term)) {
			return;
		}
		const std::string quoted = "'" + std::string(name) + "'";
		if (unanswered.find(quoted) == std::string::npos) {
			unanswered += (unanswered.empty() ? "" : ", ") + quoted;
		}
	}

	void ConditionParser::limitCount(const Operand& counted, const Operand& bound) {
		const ConditionNode& boundNode = condition.nodes[bound.node];
		if (counted.term.has_value() && boundNode.kind == NodeKind::NumberLiteral) {
			condition.nodes[counted.node].countLimit = countPast(boundNode.number);
		}
	}

	bool ConditionParser::requireTruth(const Operand& operand) {
		if (operand.type == ValueType::Truth) {
			return true;
		}
		fail(operand.position, std::string("expected a true-or-false condition, but this is ") + typeName(operand.type));
		return false;
	}

	std::optional<uint16_t> ConditionParser::addNode(const ConditionNode& node, std::size_t position) {
		if (condition.nodes.size() >= kMaxConditionNodes) {
			fail(position, "condition is too long (more than " + std::to_string(kMaxConditionNodes) + " parts)");
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
