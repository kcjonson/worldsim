#include "ConditionVocabulary.h"

#include "ConditionParser.h"
#include "KnownTerms.h"

#include <array>
#include <cassert>
#include <span>

namespace conditions {

	ConditionVocabulary::ConditionVocabulary() {
		for (TermSignature& known : knownTerms()) {
			std::string name = known.name;
			termsByName.emplace(std::move(name), static_cast<uint32_t>(terms.size()));
			terms.push_back(Term{.signature = std::move(known), .provider = nullptr});
		}
	}

	std::optional<std::string> ConditionVocabulary::provide(TermSignature signature, TermProvider provider) {
		if (!provider) {
			return "a term needs a provider";
		}
		if (signature.parameters.size() > kMaxArguments) {
			return "'" + signature.name + "' takes more than " + std::to_string(kMaxArguments) + " arguments";
		}
		const auto existing = termsByName.find(signature.name);
		if (existing == termsByName.end()) {
			termsByName.emplace(signature.name, static_cast<uint32_t>(terms.size()));
			terms.push_back(Term{.signature = std::move(signature), .provider = std::move(provider)});
			return std::nullopt;
		}

		Term& term = terms[existing->second];
		if (term.provider) {
			return "'" + signature.name + "' is already provided";
		}
		if (term.signature.result != signature.result || term.signature.parameters != signature.parameters) {
			return "'" + signature.name + "' does not match the known term's types";
		}
		term.signature.parameterSymbols.resize(signature.parameters.size());
		for (std::size_t parameter = 0; parameter < signature.parameterSymbols.size(); ++parameter) {
			if (!signature.parameterSymbols[parameter].empty()) {
				term.signature.parameterSymbols[parameter] = std::move(signature.parameterSymbols[parameter]);
			}
		}
		if (!signature.resultSymbols.empty()) {
			term.signature.resultSymbols = std::move(signature.resultSymbols);
		}
		term.provider = std::move(provider);
		return std::nullopt;
	}

	ParseResult ConditionVocabulary::parse(std::string_view text) {
		return ConditionParser(*this, text).parse();
	}

	uint32_t ConditionVocabulary::symbolId(std::string_view name) {
		const auto existing = symbols.find(std::string(name));
		if (existing != symbols.end()) {
			return existing->second;
		}
		const auto id = static_cast<uint32_t>(symbols.size() + 1);
		symbols.emplace(std::string(name), id);
		return id;
	}

	std::optional<uint32_t> ConditionVocabulary::findTerm(std::string_view name) const {
		const auto existing = termsByName.find(std::string(name));
		if (existing == termsByName.end()) {
			return std::nullopt;
		}
		return existing->second;
	}

	Truth ConditionVocabulary::evaluate(const Condition& condition, const ConditionContext& context) const {
		assert(!condition.nodes.empty() && "evaluating a condition that did not parse");
		return truthOf(condition, condition.root(), context);
	}

	Truth ConditionVocabulary::truthOf(const Condition& condition, uint16_t node, const ConditionContext& context) const {
		const ConditionNode& current = condition.nodes[node];
		switch (current.kind) {
			case NodeKind::And: {
				const Truth left = truthOf(condition, current.left, context);
				if (left == Truth::False) {
					return Truth::False;
				}
				const Truth right = truthOf(condition, current.right, context);
				if (right == Truth::False) {
					return Truth::False;
				}
				return left == Truth::True && right == Truth::True ? Truth::True : Truth::Unknown;
			}
			case NodeKind::Or: {
				const Truth left = truthOf(condition, current.left, context);
				if (left == Truth::True) {
					return Truth::True;
				}
				const Truth right = truthOf(condition, current.right, context);
				if (right == Truth::True) {
					return Truth::True;
				}
				return left == Truth::False && right == Truth::False ? Truth::False : Truth::Unknown;
			}
			case NodeKind::Not: {
				const Truth operand = truthOf(condition, current.left, context);
				if (operand == Truth::Unknown) {
					return Truth::Unknown;
				}
				return operand == Truth::True ? Truth::False : Truth::True;
			}
			case NodeKind::Compare: {
				const TermValue left = valueOf(condition, current.left, context);
				const TermValue right = valueOf(condition, current.right, context);
				if (!left.known || !right.known) {
					return Truth::Unknown;
				}
				bool holds = false;
				switch (current.comparison) {
					case Comparison::Less:
						holds = left.number < right.number;
						break;
					case Comparison::LessOrEqual:
						holds = left.number <= right.number;
						break;
					case Comparison::Greater:
						holds = left.number > right.number;
						break;
					case Comparison::GreaterOrEqual:
						holds = left.number >= right.number;
						break;
					case Comparison::Equal:
						holds = left.number == right.number;
						break;
					case Comparison::NotEqual:
						holds = left.number != right.number;
						break;
				}
				return holds ? Truth::True : Truth::False;
			}
			case NodeKind::Is: {
				const TermValue value = valueOf(condition, current.left, context);
				if (!value.known) {
					return Truth::Unknown;
				}
				return value.identity == current.symbol ? Truth::True : Truth::False;
			}
			case NodeKind::Term: {
				const TermValue value = valueOf(condition, node, context);
				if (!value.known) {
					return Truth::Unknown;
				}
				return value.number != 0.0 ? Truth::True : Truth::False;
			}
			case NodeKind::NumberLiteral:
			case NodeKind::SymbolLiteral:
				break;
		}
		assert(false && "a value node where a true-or-false one belongs; the parser rejects these");
		return Truth::Unknown;
	}

	TermValue ConditionVocabulary::valueOf(const Condition& condition, uint16_t node, const ConditionContext& context) const {
		const ConditionNode& current = condition.nodes[node];
		switch (current.kind) {
			case NodeKind::NumberLiteral:
				return TermValue::fromNumber(current.number);
			case NodeKind::SymbolLiteral:
				return TermValue::fromSymbol(current.symbol);
			case NodeKind::Term: {
				const Term& term = terms[current.term];
				if (!term.provider) {
					return TermValue::unknown();
				}
				std::array<TermValue, kMaxArguments> arguments{};
				for (uint8_t index = 0; index < current.argumentCount; ++index) {
					arguments[index] = valueOf(condition, condition.arguments[current.firstArgument + index], context);
					if (!arguments[index].known) {
						return TermValue::unknown();
					}
				}
				return term.provider(std::span<const TermValue>(arguments.data(), current.argumentCount), context);
			}
			default:
				break;
		}
		assert(false && "a true-or-false node where a value belongs; the parser rejects these");
		return TermValue::unknown();
	}

} // namespace conditions
