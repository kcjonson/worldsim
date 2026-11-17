#pragma once

#include "TermValue.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace conditions {

	/// The most nodes one condition may hold.
	inline constexpr std::size_t kMaxConditionNodes = 256;

	enum class NodeKind : uint8_t {
		And,		   // children, all Truth
		Or,			   // children, all Truth
		Not,		   // left
		Compare,	   // left and right, both Number or both Target (== and != only)
		Is,			   // left, a Symbol term, against symbol
		Term,		   // term is the vocabulary slot; children are its arguments
		NumberLiteral, // number
		SymbolLiteral, // symbol
		Self,		   // the colonist the condition is asked about
		BoundTarget	   // the entity the caller bound as `target`
	};

	enum class Comparison : uint8_t { Less, LessOrEqual, Greater, GreaterOrEqual, Equal, NotEqual };

	/// One node of a parsed condition. Children are indices into the same Condition's nodes.
	struct ConditionNode {
		double	   number = 0.0;
		uint32_t   term = 0;
		uint32_t   countLimit = std::numeric_limits<uint32_t>::max(); // a Term's TermCall::countLimit
		SymbolId   symbol{};
		uint16_t   left = 0;
		uint16_t   right = 0;
		uint16_t   firstChild = 0; // into Condition::children
		uint16_t   childCount = 0;
		NodeKind   kind = NodeKind::NumberLiteral;
		Comparison comparison = Comparison::Less;
	};

	/// A condition parsed once at load, evaluated by the vocabulary that parsed it. Nodes are stored flat,
	/// children before parents; the root is the last node.
	struct Condition {
		std::vector<ConditionNode> nodes;
		std::vector<uint16_t>	   children;
		std::string				   text;

		[[nodiscard]] uint16_t root() const { return static_cast<uint16_t>(nodes.size() - 1); }
	};

	/// Why an expression did not parse: the expression, the byte offset the problem starts at, and what it is.
	struct ConditionError {
		std::string text;
		std::size_t position = 0;
		std::string message;

		/// "<message> at column <position + 1> in '<text>'", for logs and load errors.
		[[nodiscard]] std::string describe() const { return message + " at column " + std::to_string(position + 1) + " in '" + text + "'"; }
	};

	struct ParseResult {
		Condition					  condition;
		std::optional<ConditionError> error;

		[[nodiscard]] bool ok() const { return !error.has_value(); }
	};

} // namespace conditions
