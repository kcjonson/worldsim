#pragma once

// The entry loaders' shared parts, used by the files that implement loadEntries and by nothing else: the
// message helpers, the reader of one file (FileReader, its per-entry parsers spread over the *Reading.cpp files),
// and the load itself (Loader: folder reading in EntryLoader.cpp, ids and references in EntryResolution.cpp, the
// creatures in EntryCreatureLoading.cpp). See /docs/technical/queries-and-chains.md.

#include "assets/ConfigNumber.h"
#include "assets/ConfigXml.h"
#include "assets/EntryLoader.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace assets::entry_loading {

	// --- Names and messages ----------------------------------------------------------------------------------

	std::string inQuotes(std::string_view text);

	template <typename Names>
	std::string joined(const Names& names) {
		std::string text;
		for (const auto& name : names) {
			text += (text.empty() ? "" : ", ") + std::string(name);
		}
		return text;
	}

	std::string capabilityNames();
	std::string numberText(float value);

	/// The part of an id before its last '/', the folder its short ids resolve in; empty for a top-level id.
	std::string_view folderOf(std::string_view id);

	/// The last part of an id, as its file declares it.
	std::string_view shortOf(std::string_view id);

	/// How a message names an entry: `<chain id="fetch_food">`, by its short id.
	std::string entryLabel(std::string_view element, std::string_view id);
	std::string stepLabel(std::size_t index, std::string_view element);
	std::string rowLabel(std::size_t index, std::string_view element);

	std::string_view targetName(const ActionTarget& target);
	bool			 sameTarget(const ActionTarget& left, const ActionTarget& right);

	inline constexpr std::array<std::string_view, 3> kStepTargetNames = {"target", "source", "destination"};
	inline constexpr std::array<std::string_view, 3> kStepAmountNames = {"for_goal", "meal", "carried"};
	inline constexpr std::array<std::string_view, 3> kRecipientNames = {"self", "target", "witnesses"};
	inline constexpr std::array<std::string_view, 9> kWorkGoalNames =
		{"haul", "harvest", "clear", "craft", "build", "deconstruct", "place_packaged", "care", "root"};
	inline constexpr std::array<std::string_view, 4> kReliefNames = {"none", "small", "medium", "large"};
	inline constexpr std::string_view				 kReservedRung = "survival";

	template <typename Names>
	std::optional<std::size_t> indexIn(const Names& names, std::string_view name) {
		for (std::size_t index = 0; index < names.size(); ++index) {
			if (std::string_view(names[index]) == name) {
				return index;
			}
		}
		return std::nullopt;
	}

	std::optional<uint16_t> factIndex(std::string_view name);

	// --- Reading one file ------------------------------------------------------------------------------------

	/// What a file is, which decides the root it must have, what its entries may hold, and how their ids are made.
	enum class FileKind : uint8_t { Behavior, Actions, Work, Orders, Facts, CreatureBlock };

	/// The rows a creature's `<creature>` block and files give, before they are settled against its behaviors.
	struct CreatureNeedRow {
		std::string								source;
		std::string								id;
		std::string								from;
		std::optional<std::string>				label;
		std::optional<float>					seek;
		std::optional<float>					urgent;
		std::optional<float>					critical;
		std::optional<NeedMove>					move;
		std::optional<std::vector<SatisfiedBy>> satisfiedBy;
	};

	struct CreatureInclude {
		std::string source;
		std::size_t row = 0;
		std::string id;
	};

	struct CreatureCostRow {
		std::string source;
		std::string need;
		float		cost = 0.0F;
	};

	struct CreatureRows {
		std::string					 name;
		std::string					 primarySource;
		bool						 mood = false;
		std::vector<CreatureInclude> includes;
		std::vector<CreatureNeedRow> needs;
		std::vector<CreatureCostRow> costs;
		/// The creature's own behavior files, by full id, in file name order.
		std::vector<std::string> ownBehaviors;
	};

	/// The entries one file holds, appended to the library only when the file has no problem.
	struct FileEntries {
		std::vector<BehaviorEntry>				behaviors;
		std::vector<NeedEntry>					needs;
		std::vector<ChainEntry>					chains;
		std::vector<QueryEntry>					queries;
		std::vector<ActionEntry>				actions;
		std::vector<CategoryEntry>				categories;
		std::vector<WorkEntry>					work;
		std::vector<OrderEntry>					orders;
		std::vector<std::pair<uint16_t, float>> factValues;
	};

	class Loader;

	class FileReader {
	  public:
		FileReader(Loader& loader, std::string source, std::string folder, FileKind kind)
			: loader(loader),
			  source(std::move(source)),
			  folder(std::move(folder)),
			  kind(kind),
			  reader(this->source) {}

		/// Every problem this file had, its own and its numbers'.
		std::vector<std::string> takeProblems() {
			std::vector<std::string> numbers = reader.takeProblems();
			problems.insert(problems.end(), numbers.begin(), numbers.end());
			return std::exchange(problems, {});
		}

		void readBehavior(pugi::xml_node root, std::string_view fullId);
		void readActions(pugi::xml_node root);
		void readWork(pugi::xml_node root);
		void readOrders(pugi::xml_node root);
		void readFacts(pugi::xml_node root);
		void readCreatureBlock(pugi::xml_node block, bool primary, CreatureRows& rows);

		FileEntries entries;

	  private:
		void fail(const std::string& where, const std::string& what) { problems.push_back(source + ": " + where + " " + what); }
		void failAt(const std::string& what) { problems.push_back(source + ": " + what); }

		bool attributesOnly(pugi::xml_node node, std::initializer_list<std::string_view> allowed, const std::string& label) {
			return onlyAttributes(node, allowed, label, source, problems);
		}

		bool childrenOnly(pugi::xml_node node, std::initializer_list<std::string_view> allowed, const std::string& label) {
			return onlyChildren(node, allowed, label, source, problems);
		}

		void refuse(const std::string& label, pugi::xml_node node, const char* attribute, const std::string& expected) {
			reader.refuseAttribute(label, node, attribute, expected);
		}

		/// The attribute's text; a problem when it is left out or empty.
		std::string required(pugi::xml_node node, const char* attribute, const std::string& label) {
			const pugi::xml_attribute found = node.attribute(attribute);
			if (found.empty() || std::string_view(found.value()).empty()) {
				if (found.empty()) {
					refuse(label, node, attribute, "");
				} else {
					refuse(label, node, attribute, "a name");
				}
				return {};
			}
			return found.value();
		}

		/// The script attribute every entry reserves for a later Lua hook: given, it is a problem, so no script is
		/// ever silently ignored.
		void reserveScript(pugi::xml_node node, const std::string& label) {
			if (!node.attribute("script").empty()) {
				refuse(label, node, "script", "left out; it is reserved for a Lua hook that runs nothing yet");
			}
		}

		/// A declared id: the short form, without a folder.
		std::string declaredId(pugi::xml_node node, std::string_view element) {
			const std::string id = node.attribute("id").value();
			const std::string label = "<" + std::string(element) + ">";
			if (node.attribute("id").empty()) {
				refuse(label, node, "id", "");
				return {};
			}
			if (id.empty() || id.find('/') != std::string::npos) {
				refuse(label, node, "id", "a short id, without a folder");
				return {};
			}
			return id;
		}

		std::string fullId(std::string_view shortId) const {
			return folder.empty() ? std::string(shortId) : folder + "/" + std::string(shortId);
		}

		template <typename Names>
		std::optional<std::size_t>
		choice(pugi::xml_node node, const char* attribute, const std::string& label, const Names& names, bool isRequired) {
			const pugi::xml_attribute found = node.attribute(attribute);
			if (found.empty()) {
				if (isRequired) {
					refuse(label, node, attribute, "");
				}
				return std::nullopt;
			}
			const std::optional<std::size_t> index = indexIn(names, found.value());
			if (!index) {
				refuse(label, node, attribute, "one of " + joined(names));
			}
			return index;
		}

		std::optional<bool> boolean(pugi::xml_node node, const char* attribute, const std::string& label) {
			return reader.labeledBoolean(label, node, attribute);
		}

		float number(pugi::xml_node node, const char* attribute, const std::string& label) {
			return reader.labeledNumber(label, node, attribute);
		}

		std::optional<float> optionalNumber(pugi::xml_node node, const char* attribute, const std::string& label) {
			if (node.attribute(attribute).empty()) {
				return std::nullopt;
			}
			return number(node, attribute, label);
		}

		/// A number that must lie in [minimum, maximum] (either bound may be open with `exclusive`).
		float boundedNumber(
			pugi::xml_node	   node,
			const char*		   attribute,
			const std::string& label,
			float			   minimum,
			float			   maximum,
			bool			   aboveMinimum = false
		) {
			const float value = number(node, attribute, label);
			if (std::isnan(value)) {
				return value;
			}
			const bool tooLow = aboveMinimum ? value <= minimum : value < minimum;
			if (tooLow || value > maximum) {
				std::string expected;
				if (maximum == std::numeric_limits<float>::max()) {
					expected = aboveMinimum ? "a number above " + numberText(minimum) : "a number, " + numberText(minimum) + " or more";
				} else {
					expected = "a number from " + numberText(minimum) + " to " + numberText(maximum);
				}
				refuse(label, node, attribute, expected);
			}
			return value;
		}

		std::optional<CapabilityType> capability(pugi::xml_node node, const char* attribute, const std::string& label, bool isRequired) {
			const pugi::xml_attribute found = node.attribute(attribute);
			if (found.empty()) {
				if (isRequired) {
					refuse(label, node, attribute, "");
				}
				return std::nullopt;
			}
			const std::optional<CapabilityType> parsed = capabilityFromName(found.value());
			if (!parsed) {
				refuse(label, node, attribute, "a capability (" + capabilityNames() + ")");
			}
			return parsed;
		}

		std::optional<uint32_t> definition(pugi::xml_node node, const char* attribute, const std::string& label);
		std::optional<ItemArgument>
							   itemArgument(pugi::xml_node node, const char* attribute, const std::string& label, bool capabilityAllowed);
		std::optional<FactUse> fact(pugi::xml_node node, const char* attribute, const std::string& label);
		std::optional<NeedId>  needName(pugi::xml_node node, const char* attribute, const std::string& label);

		template <typename Entry>
		EntryReference<Entry> reference(pugi::xml_node node, const char* attribute, const std::string& label) {
			return EntryReference<Entry>{.written = required(node, attribute, label), .handle = {}};
		}

		std::optional<NeedMove>		   needMove(pugi::xml_node node, const std::string& label, bool isRequired);
		std::vector<SatisfiedBy>	   satisfiedByRows(pugi::xml_node node, const std::string& label);
		std::optional<NeedEntry>	   need(pugi::xml_node node);
		void						   levelOrder(pugi::xml_node node, const std::string& label, float seek, float urgent, float critical);
		std::optional<ChainEntry>	   chain(pugi::xml_node node);
		std::optional<QueryEntry>	   query(pugi::xml_node node);
		std::optional<FromRow>		   fromRow(pugi::xml_node node, const std::string& label);
		std::optional<ActionEntry>	   action(pugi::xml_node node);
		std::optional<EffectRow>	   effect(pugi::xml_node node, const std::string& label);
		std::optional<ActionEntry>	   actionGroup(pugi::xml_node node);
		std::optional<LowMoodEntry>	   lowMood(pugi::xml_node node, const std::string& behaviorLabel);
		std::optional<CategoryEntry>   category(pugi::xml_node node);
		std::optional<WorkEntry>	   workEntry(pugi::xml_node node);
		std::optional<OrderEntry>	   order(pugi::xml_node node);
		std::optional<CreatureNeedRow> creatureNeed(pugi::xml_node node, const std::string& label);

		Loader&					 loader;
		std::string				 source;
		std::string				 folder;
		FileKind				 kind;
		ConfigReader			 reader;
		std::vector<std::string> problems;
	};

	// --- The load --------------------------------------------------------------------------------------------

	class Loader {
	  public:
		explicit Loader(const EntrySources& sources)
			: sources(sources) {
			result.library.factTuning.fill(std::numeric_limits<float>::quiet_NaN());
		}

		EntryLoad run();

		NeedId intern(const std::string& name) {
			EntryLibrary& library = result.library;
			const auto [found, added] = library.needIndex.try_emplace(name, static_cast<uint32_t>(library.needIds.size()));
			if (added) {
				library.needIds.push_back(name);
			}
			return NeedId{static_cast<uint16_t>(found->second)};
		}

		[[nodiscard]] const std::vector<std::string>& rungs() const { return sources.rungs; }
		[[nodiscard]] std::optional<uint32_t>		  definitionId(std::string_view name) const {
			return sources.definitionId ? sources.definitionId(name) : std::nullopt;
		}

	  private:
		void readSharedFolder(std::string_view folderName, const char* rootName, FileKind kind);
		void readBehaviorsFolder();
		void readCreatures();
		void readBehaviorFile(const std::filesystem::path& file, const std::string& source, const std::string& fullId);
		void append(FileReader& file);
		void checkFactTuning();

		void registerIds();
		void resolve();
		void resolveCreatures();
		void checkChains();
		void warnUnstarted();

		template <typename Entry>
		void registerEach(std::vector<Entry>& entries, EntryLibrary::IdIndex& index, std::string_view element, std::string_view typeName);
		template <typename Entry>
		bool resolveReference(
			EntryReference<Entry>&		 reference,
			const EntryLibrary::IdIndex& index,
			std::string_view			 inFolder,
			const std::string&			 where,
			const char*					 attribute,
			std::string_view			 typeName
		);
		void checkNeedDefined(NeedId need, const std::string& where, const char* attribute);
		void resolveSatisfiedBy(std::vector<SatisfiedBy>& rows, std::string_view inFolder, const std::string& where);

		void fail(std::string message) { result.errors.push_back(std::move(message)); }
		void warn(std::string message) {
			LOG_WARNING(Engine, "%s", message.c_str());
			result.warnings.push_back(std::move(message));
		}

		const EntrySources&		  sources;
		EntryLoad				  result;
		std::vector<CreatureRows> creatureRows;
		/// Which file tuned each fact, for the duplicate's message.
		std::array<std::string, kFacts.size()> factSources;
		/// The need ids some behavior or creature has; an effect may name only these.
		std::set<uint16_t> definedNeeds;
	};

} // namespace assets::entry_loading
