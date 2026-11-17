#include "ConditionVocabulary.h"

#include "ConditionParser.h"

#include <utils/Log.h>

#include <algorithm>
#include <array>
#include <cassert>

namespace conditions {

	std::vector<std::string> ConditionVocabulary::declare(const assets::ConditionConfig& config) {
		std::vector<std::string> errors;
		if (frozen) {
			errors.emplace_back("the condition vocabulary is frozen; nothing more can be declared");
			return errors;
		}
		for (const auto& [name, set] : config.symbolSets) {
			declareSymbolSet(name, set, errors);
		}

		for (const assets::ConditionTermDef& declaration : config.terms) {
			const std::string where =
				declaration.source.empty() ? "term '" + declaration.name + "'" : declaration.source + ": term '" + declaration.name + "'";
			if (!isConditionName(declaration.name)) {
				errors.push_back(where + " is not a name a condition can use (letters, digits, and _, not a word of the grammar)");
				continue;
			}
			if (termsByName.contains(declaration.name)) {
				errors.push_back(where + " is already declared");
				continue;
			}
			if (declaration.parameters.size() > kMaxArguments) {
				errors.push_back(where + " takes more than " + std::to_string(kMaxArguments) + " arguments");
				continue;
			}

			TermSignature signature{.name = declaration.name, .result = declaration.result};
			bool		  complete = true;
			const auto	  findSet = [&](const std::string& set) -> std::optional<uint32_t> {
				   const auto found = symbolSetsByName.find(set);
				   if (found == symbolSetsByName.end()) {
					   errors.push_back(where + " takes names from \"" + set + "\", which is not declared");
					   complete = false;
					   return std::nullopt;
				   }
				   return found->second;
			};
			if (declaration.result == ValueType::Symbol) {
				signature.resultSymbolSet = findSet(declaration.resultSymbols);
			}
			for (const assets::ConditionParameterDef& parameter : declaration.parameters) {
				TermParameter declared{.type = parameter.type};
				if (parameter.type == ValueType::Symbol) {
					declared.symbolSet = findSet(parameter.symbols);
				}
				signature.parameters.push_back(declared);
			}
			if (!complete) {
				continue;
			}
			termsByName.emplace(declaration.name, static_cast<uint32_t>(terms.size()));
			terms.push_back(Term{.signature = std::move(signature), .provider = nullptr});
		}
		return errors;
	}

	void ConditionVocabulary::declareSymbolSet(
		const std::string&					name,
		const assets::ConditionSymbolSetDef& declared,
		std::vector<std::string>&			errors
	) {
		if (symbolSetsByName.contains(name)) {
			errors.push_back("symbol set \"" + name + "\" is already declared");
			return;
		}
		SymbolSet set{.name = name, .open = declared.open};
		for (const std::string& symbol : declared.names) {
			if (!isConditionName(symbol)) {
				errors.push_back("'" + symbol + "' in \"" + name + "\" is not a name a condition can use");
				continue;
			}
			addSymbol(set, symbol);
		}
		const auto index = static_cast<uint32_t>(symbolSets.size());
		symbolSetsByName.emplace(name, index);
		symbolSets.push_back(std::move(set));
	}

	void ConditionVocabulary::addSymbol(SymbolSet& set, const std::string& name) {
		if (std::ranges::find(set.symbols, name, &DeclaredSymbol::name) == set.symbols.end()) {
			set.symbols.push_back(DeclaredSymbol{.name = name, .id = intern(name)});
		}
	}

	std::optional<std::string>
	ConditionVocabulary::addSymbols(std::string_view term, std::size_t parameter, std::span<const std::string> names) {
		if (frozen) {
			return "the condition vocabulary is frozen; names for '" + std::string(term) + "' cannot be added now";
		}
		const std::optional<uint32_t> slot = findTerm(term);
		if (!slot.has_value()) {
			return "'" + std::string(term) + "' is not declared in the condition vocabulary";
		}
		const TermSignature& declared = terms[*slot].signature;
		if (parameter >= declared.parameters.size() || !declared.parameters[parameter].symbolSet.has_value()) {
			return "argument " + std::to_string(parameter + 1) + " of '" + std::string(term) + "' does not take names";
		}
		SymbolSet& set = symbolSets[*declared.parameters[parameter].symbolSet];
		for (const std::string& name : names) {
			if (!isConditionName(name)) {
				return "'" + name + "' for '" + std::string(term) + "' is not a name a condition can use";
			}
			addSymbol(set, name);
		}
		return std::nullopt;
	}

	SymbolId ConditionVocabulary::intern(const std::string& name) {
		return symbols.try_emplace(name, static_cast<SymbolId>(symbols.size() + 1)).first->second;
	}

	std::optional<std::string>
	ConditionVocabulary::provide(std::string_view name, ValueType result, std::vector<ValueType> parameters, TermProvider provider) {
		if (frozen) {
			return "the condition vocabulary is frozen; '" + std::string(name) + "' cannot be provided now";
		}
		if (!provider) {
			return "'" + std::string(name) + "' needs a provider";
		}
		const std::optional<uint32_t> term = findTerm(name);
		if (!term.has_value()) {
			return "'" + std::string(name) + "' is not declared in the condition vocabulary";
		}
		Term& declared = terms[*term];
		if (declared.provider) {
			return "'" + std::string(name) + "' is already provided";
		}
		const bool parametersMatch = std::ranges::equal(parameters, declared.signature.parameters, {}, {}, &TermParameter::type);
		if (declared.signature.result != result || !parametersMatch) {
			return "'" + std::string(name) + "' is provided with types that do not match its declaration";
		}
		declared.provider = std::move(provider);
		return std::nullopt;
	}

	ParseResult ConditionVocabulary::parse(std::string_view text) const {
		if (!frozen) {
			return {
				.condition = {},
				.error = ConditionError{
					.text = std::string(text),
					.position = 0,
					.message = "the condition vocabulary is still being built; parse after it is frozen"
				}
			};
		}
		ConditionParser parser(*this, text);
		ParseResult		result = parser.parse();
		if (!result.ok()) {
			return result;
		}
		if (!parser.unansweredNames().empty()) {
			LOG_INFO(
				Game,
				"Condition '%s' names %s, which nothing answers; it reads Unknown",
				result.condition.text.c_str(),
				parser.unansweredNames().c_str()
			);
		}

		std::string unprovided;
		for (const ConditionNode& node : result.condition.nodes) {
			if (node.kind != NodeKind::Term || isProvided(node.term)) {
				continue;
			}
			const std::string& name = terms[node.term].signature.name;
			const std::string  quoted = "'" + name + "'";
			if (unprovided.find(quoted) == std::string::npos) {
				unprovided += (unprovided.empty() ? "" : ", ") + quoted;
			}
		}
		if (!unprovided.empty()) {
			LOG_INFO(
				Game, "Condition '%s' uses %s, which nothing provides; it reads Unknown", result.condition.text.c_str(), unprovided.c_str()
			);
		}
		return result;
	}

	std::span<const DeclaredSymbol> ConditionVocabulary::parameterSymbols(std::string_view term, std::size_t parameter) const {
		const std::optional<uint32_t> slot = findTerm(term);
		if (!slot.has_value()) {
			return {};
		}
		const TermSignature& declared = terms[*slot].signature;
		if (parameter >= declared.parameters.size() || !declared.parameters[parameter].symbolSet.has_value()) {
			return {};
		}
		return symbolSets[*declared.parameters[parameter].symbolSet].symbols;
	}

	std::optional<SymbolId> ConditionVocabulary::findSymbol(std::string_view name) const {
		const auto existing = symbols.find(std::string(name));
		if (existing == symbols.end()) {
			return std::nullopt;
		}
		return existing->second;
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
			case NodeKind::And:
			case NodeKind::Or: {
				// And stops at the first False, Or at the first True; otherwise Unknown wins over the other.
				const Truth decisive = current.kind == NodeKind::And ? Truth::False : Truth::True;
				Truth		answer = current.kind == NodeKind::And ? Truth::True : Truth::False;
				for (uint16_t index = 0; index < current.childCount; ++index) {
					const Truth operand = truthOf(condition, condition.children[current.firstChild + index], context);
					if (operand == decisive) {
						return decisive;
					}
					if (operand == Truth::Unknown) {
						answer = Truth::Unknown;
					}
				}
				return answer;
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
				if (!left.known() || !right.known()) {
					return Truth::Unknown;
				}
				if (left.type() == ValueType::Target) {
					const bool same = left.target() == right.target();
					return same == (current.comparison == Comparison::Equal) ? Truth::True : Truth::False;
				}
				bool holds = false;
				switch (current.comparison) {
					case Comparison::Less:
						holds = left.number() < right.number();
						break;
					case Comparison::LessOrEqual:
						holds = left.number() <= right.number();
						break;
					case Comparison::Greater:
						holds = left.number() > right.number();
						break;
					case Comparison::GreaterOrEqual:
						holds = left.number() >= right.number();
						break;
					case Comparison::Equal:
						holds = left.number() == right.number();
						break;
					case Comparison::NotEqual:
						holds = left.number() != right.number();
						break;
				}
				return holds ? Truth::True : Truth::False;
			}
			case NodeKind::Is: {
				const TermValue value = valueOf(condition, current.left, context);
				if (!value.known() || current.symbol == kUnlistedSymbol) {
					return Truth::Unknown;
				}
				return value.symbol() == current.symbol ? Truth::True : Truth::False;
			}
			case NodeKind::Term: {
				const TermValue value = valueOf(condition, node, context);
				if (!value.known()) {
					return Truth::Unknown;
				}
				return value.truth() ? Truth::True : Truth::False;
			}
			case NodeKind::NumberLiteral:
			case NodeKind::SymbolLiteral:
			case NodeKind::Self:
			case NodeKind::BoundTarget:
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
			case NodeKind::Self:
				return TermValue::fromTarget(context.colonist);
			case NodeKind::BoundTarget:
				return TermValue::fromTarget(context.target);
			case NodeKind::Term: {
				const Term& term = terms[current.term];
				if (!term.provider) {
					return TermValue::unknown();
				}
				std::array<TermValue, kMaxArguments> arguments{};
				for (uint16_t index = 0; index < current.childCount; ++index) {
					arguments[index] = valueOf(condition, condition.children[current.firstChild + index], context);
					if (!arguments[index].known()) {
						return TermValue::unknown();
					}
				}
				const TermValue value = term.provider(
					TermCall{
						.arguments = std::span<const TermValue>(arguments.data(), current.childCount),
						.context = context,
						.countLimit = current.countLimit
					}
				);
				assert((!value.known() || value.type() == term.signature.result) && "a provider answered with the wrong type");
				return value;
			}
			default:
				break;
		}
		assert(false && "a true-or-false node where a value belongs; the parser rejects these");
		return TermValue::unknown();
	}

} // namespace conditions
