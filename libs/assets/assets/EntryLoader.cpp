#include "assets/EntryLoader.h"

#include "assets/ConfigFiles.h"
#include "assets/ConfigNumber.h"
#include "assets/ConfigXml.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <algorithm>
#include <bit>
#include <cassert>
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <type_traits>
#include <utility>

namespace assets {

	const CreatureEntry* EntryLibrary::findCreature(std::string_view name) const {
		const auto found = std::ranges::find(creatures, name, &CreatureEntry::name);
		return found == creatures.end() ? nullptr : &*found;
	}

	std::optional<NeedId> EntryLibrary::findNeed(std::string_view id) const {
		const auto found = needIndex.find(id);
		if (found == needIndex.end()) {
			return std::nullopt;
		}
		return NeedId{static_cast<uint16_t>(found->second)};
	}

	namespace {

		// --- Names and messages ----------------------------------------------------------------------------

		std::string inQuotes(std::string_view text) {
			return "\"" + std::string(text) + "\"";
		}

		template <typename Names>
		std::string joined(const Names& names) {
			std::string text;
			for (const auto& name : names) {
				text += (text.empty() ? "" : ", ") + std::string(name);
			}
			return text;
		}

		std::string capabilityNames() {
			return joined(kCapabilityNames);
		}

		std::string numberText(float value) {
			std::array<char, 32> buffer{};
			const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
			return error == std::errc() ? std::string(buffer.data(), end) : std::string("?");
		}

		/// The part of an id before its last '/', the folder its short ids resolve in; empty for a top-level id.
		std::string_view folderOf(std::string_view id) {
			const std::size_t slash = id.rfind('/');
			return slash == std::string_view::npos ? std::string_view() : id.substr(0, slash);
		}

		/// The last part of an id, as its file declares it.
		std::string_view shortOf(std::string_view id) {
			const std::size_t slash = id.rfind('/');
			return slash == std::string_view::npos ? id : id.substr(slash + 1);
		}

		/// How a message names an entry: `<chain id="fetch_food">`, by its short id.
		std::string entryLabel(std::string_view element, std::string_view id) {
			return "<" + std::string(element) + " id=" + inQuotes(shortOf(id)) + ">";
		}

		std::string stepLabel(std::size_t index, std::string_view element) {
			return "step " + std::to_string(index + 1) + " <" + std::string(element) + ">";
		}

		std::string rowLabel(std::size_t index, std::string_view element) {
			return "row " + std::to_string(index + 1) + " <" + std::string(element) + ">";
		}

		std::string_view targetName(const ActionTarget& target) {
			switch (target.kind) {
				case ActionTarget::Kind::LooseItem:
					return "loose_item";
				case ActionTarget::Kind::Packaged:
					return "packaged";
				case ActionTarget::Kind::Capability:
					break;
			}
			return capabilityName(target.capability);
		}

		bool sameTarget(const ActionTarget& left, const ActionTarget& right) {
			return left.kind == right.kind && (left.kind != ActionTarget::Kind::Capability || left.capability == right.capability);
		}

		constexpr std::array<std::string_view, 3> kStepTargetNames = {"target", "source", "destination"};
		constexpr std::array<std::string_view, 3> kStepAmountNames = {"for_goal", "meal", "carried"};
		constexpr std::array<std::string_view, 3> kRecipientNames = {"self", "target", "witnesses"};
		constexpr std::array<std::string_view, 9> kWorkGoalNames =
			{"haul", "harvest", "clear", "craft", "build", "deconstruct", "place_packaged", "care", "root"};
		constexpr std::array<std::string_view, 4> kReliefNames = {"none", "small", "medium", "large"};
		constexpr std::string_view				  kReservedRung = "survival";

		template <typename Names>
		std::optional<std::size_t> indexIn(const Names& names, std::string_view name) {
			for (std::size_t index = 0; index < names.size(); ++index) {
				if (std::string_view(names[index]) == name) {
					return index;
				}
			}
			return std::nullopt;
		}

		std::optional<uint16_t> factIndex(std::string_view name) {
			const auto found = std::ranges::find(kFacts, name, &FactDeclaration::name);
			if (found == kFacts.end()) {
				return std::nullopt;
			}
			return static_cast<uint16_t>(found - kFacts.begin());
		}

		// --- Reading one file ------------------------------------------------------------------------------

		/// What a file is, which decides the root it must have, what its entries may hold, and how their ids
		/// are made.
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
				bool clean = true;
				for (const pugi::xml_node child : node.children()) {
					if (child.type() != pugi::node_element) {
						continue;
					}
					if (std::ranges::find(allowed, std::string_view(child.name())) == allowed.end()) {
						failAt(label + ": unknown element <" + child.name() + ">");
						clean = false;
					}
				}
				return clean;
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

			std::optional<CapabilityType>
			capability(pugi::xml_node node, const char* attribute, const std::string& label, bool isRequired) {
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

			std::optional<NeedMove>		needMove(pugi::xml_node node, const std::string& label, bool isRequired);
			std::vector<SatisfiedBy>	satisfiedByRows(pugi::xml_node node, const std::string& label);
			std::optional<NeedEntry>	need(pugi::xml_node node);
			void						levelOrder(pugi::xml_node node, const std::string& label, float seek, float urgent, float critical);
			std::optional<ChainEntry>	chain(pugi::xml_node node);
			std::optional<QueryEntry>	query(pugi::xml_node node);
			std::optional<FromRow>		fromRow(pugi::xml_node node, const std::string& label);
			std::optional<ActionEntry>	action(pugi::xml_node node);
			std::optional<EffectRow>	effect(pugi::xml_node node, const std::string& label);
			std::optional<ActionEntry>	actionGroup(pugi::xml_node node);
			std::optional<LowMoodEntry> lowMood(pugi::xml_node node, const std::string& behaviorLabel);
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

		// --- The load ------------------------------------------------------------------------------------

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
			void
			registerEach(std::vector<Entry>& entries, EntryLibrary::IdIndex& index, std::string_view element, std::string_view typeName);
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

		// --- FileReader: values ----------------------------------------------------------------------------

		std::optional<uint32_t> FileReader::definition(pugi::xml_node node, const char* attribute, const std::string& label) {
			const std::string name = required(node, attribute, label);
			if (name.empty()) {
				return std::nullopt;
			}
			const std::optional<uint32_t> id = loader.definitionId(name);
			if (!id) {
				fail(label, std::string(attribute) + " " + inQuotes(name) + " names no definition");
			}
			return id;
		}

		std::optional<ItemArgument>
		FileReader::itemArgument(pugi::xml_node node, const char* attribute, const std::string& label, bool capabilityAllowed) {
			const std::string written = required(node, attribute, label);
			if (written.empty()) {
				return std::nullopt;
			}
			ItemArgument argument{.written = written};
			if (written == "goal") {
				if (kind != FileKind::Work) {
					fail(
						label,
						std::string(attribute) + " " + inQuotes(written) +
							" is the item a work option is for, which only a query in work/ has"
					);
					return std::nullopt;
				}
				argument.kind = ItemArgument::Kind::Goal;
				return argument;
			}
			if (const std::optional<CapabilityType> named = capabilityFromName(written); named && capabilityAllowed) {
				argument.kind = ItemArgument::Kind::Capability;
				argument.capability = *named;
				return argument;
			}
			const std::optional<uint32_t> id = loader.definitionId(written);
			if (!id) {
				fail(
					label,
					std::string(attribute) + " " + inQuotes(written) +
						(capabilityAllowed ? " names no definition or capability" : " names no definition")
				);
				return std::nullopt;
			}
			argument.kind = ItemArgument::Kind::Definition;
			argument.defNameId = *id;
			return argument;
		}

		std::optional<FactUse> FileReader::fact(pugi::xml_node node, const char* attribute, const std::string& label) {
			const std::string written = required(node, attribute, label);
			if (written.empty()) {
				return std::nullopt;
			}
			std::string_view  name = written;
			std::string		  argument;
			const std::size_t open = written.find('(');
			if (open != std::string::npos) {
				if (written.back() != ')' || written.size() < open + 3) {
					fail(
						label,
						std::string(attribute) + " " + inQuotes(written) + " must be a fact, or a fact with one argument as contains(tag)"
					);
					return std::nullopt;
				}
				name = std::string_view(written).substr(0, open);
				argument = written.substr(open + 1, written.size() - open - 2);
			}
			const std::optional<uint16_t> index = factIndex(name);
			if (!index) {
				fail(label, std::string(attribute) + " " + inQuotes(written) + " names no fact the sim provides");
				return std::nullopt;
			}
			if (kFacts[*index].takesArgument && argument.empty()) {
				fail(label, std::string(attribute) + " " + inQuotes(written) + " takes one argument, as " + std::string(name) + "(tag)");
				return std::nullopt;
			}
			if (!kFacts[*index].takesArgument && !argument.empty()) {
				fail(label, std::string(attribute) + " " + inQuotes(written) + " takes no argument");
				return std::nullopt;
			}
			return FactUse{.fact = *index, .argument = argument};
		}

		std::optional<NeedId> FileReader::needName(pugi::xml_node node, const char* attribute, const std::string& label) {
			const std::string name = required(node, attribute, label);
			if (name.empty()) {
				return std::nullopt;
			}
			if (name.find('/') != std::string::npos) {
				refuse(label, node, attribute, "a need id, a plain name");
				return std::nullopt;
			}
			return loader.intern(name);
		}

		// --- FileReader: needs ---------------------------------------------------------------------------

		std::optional<NeedMove> FileReader::needMove(pugi::xml_node node, const std::string& label, bool isRequired) {
			const pugi::xml_node drains = node.child("drains");
			const pugi::xml_node setBy = node.child("setBy");
			const std::size_t	 given =
				static_cast<std::size_t>(std::distance(node.children("drains").begin(), node.children("drains").end())) +
				static_cast<std::size_t>(std::distance(node.children("setBy").begin(), node.children("setBy").end()));
			if (given > 1) {
				failAt(label + " has more than one of <drains> and <setBy>; a level moves in exactly one way");
				return std::nullopt;
			}
			if (given == 0) {
				if (isRequired) {
					failAt(label + " has neither <drains> nor <setBy>; a level moves in exactly one way");
				}
				return std::nullopt;
			}
			NeedMove move;
			if (drains) {
				const std::string drainsLabel = label + " <drains>";
				attributesOnly(drains, {"perGameMinute"}, drainsLabel);
				childrenOnly(drains, {"rate"}, drainsLabel);
				move.kind = NeedMove::Kind::Drains;
				move.perGameMinute = boundedNumber(drains, "perGameMinute", drainsLabel, 0.0F, std::numeric_limits<float>::max());
				std::size_t row = 0;
				for (const pugi::xml_node rate : drains.children("rate")) {
					const std::string rateLabel = drainsLabel + " " + rowLabel(row++, "rate");
					attributesOnly(rate, {"when", "multiply"}, rateLabel);
					move.rates.push_back(
						RateRow{
							.when = required(rate, "when", rateLabel),
							.multiply = boundedNumber(rate, "multiply", rateLabel, 0.0F, std::numeric_limits<float>::max()),
						}
					);
				}
				return move;
			}
			const std::string setByLabel = label + " <setBy>";
			attributesOnly(setBy, {"provider"}, setByLabel);
			childrenOnly(setBy, {}, setByLabel);
			move.kind = NeedMove::Kind::SetBy;
			if (const std::optional<std::size_t> provider = choice(setBy, "provider", setByLabel, kNeedProviders, true)) {
				move.provider = std::string(kNeedProviders[*provider]);
			}
			return move;
		}

		std::vector<SatisfiedBy> FileReader::satisfiedByRows(pugi::xml_node node, const std::string& label) {
			std::vector<SatisfiedBy> rows;
			std::size_t				 index = 0;
			for (const pugi::xml_node row : node.children("satisfiedBy")) {
				const std::string rowName = label + " " + rowLabel(index++, "satisfiedBy");
				attributesOnly(row, {"chain", "work", "when"}, rowName);
				childrenOnly(row, {}, rowName);
				SatisfiedBy satisfied;
				const bool	hasChain = !row.attribute("chain").empty();
				const bool	hasWork = !row.attribute("work").empty();
				if (hasChain == hasWork) {
					failAt(
						rowName + " names " + (hasChain ? "both a chain and a work entry" : "neither a chain nor a work entry") +
						"; give exactly one"
					);
				} else if (hasChain) {
					satisfied.chain = reference<ChainEntry>(row, "chain", rowName);
				} else {
					satisfied.work = reference<WorkEntry>(row, "work", rowName);
				}
				if (!row.attribute("when").empty()) {
					satisfied.when = required(row, "when", rowName);
				}
				rows.push_back(std::move(satisfied));
			}
			return rows;
		}

		void FileReader::levelOrder(pugi::xml_node node, const std::string& label, float seek, float urgent, float critical) {
			if (!std::isnan(seek) && !std::isnan(urgent) && urgent >= seek) {
				refuse(label, node, "urgent", "below seek (" + numberText(seek) + ")");
			}
			if (!std::isnan(urgent) && !std::isnan(critical) && critical >= urgent) {
				refuse(label, node, "critical", "below urgent (" + numberText(urgent) + ")");
			}
		}

		std::optional<NeedEntry> FileReader::need(pugi::xml_node node) {
			const std::string label = "<need id=" + inQuotes(node.attribute("id").value()) + ">";
			attributesOnly(node, {"id", "label", "seek", "urgent", "critical", "script"}, label);
			childrenOnly(node, {"drains", "setBy", "satisfiedBy"}, label);
			reserveScript(node, label);
			NeedEntry					entry;
			const std::optional<NeedId> id = needName(node, "id", "<need>");
			entry.label = required(node, "label", label);
			entry.seek = boundedNumber(node, "seek", label, 0.0F, 100.0F);
			entry.urgent = boundedNumber(node, "urgent", label, 0.0F, 100.0F);
			entry.critical = boundedNumber(node, "critical", label, 0.0F, 100.0F);
			levelOrder(node, label, entry.seek, entry.urgent, entry.critical);
			const std::optional<NeedMove> move = needMove(node, label, true);
			entry.satisfiedBy = satisfiedByRows(node, label);
			entry.file = source;
			if (!id || !move) {
				return std::nullopt;
			}
			entry.id = *id;
			entry.move = *move;
			return entry;
		}

		// --- FileReader: chains ----------------------------------------------------------------------------

		std::optional<ChainEntry> FileReader::chain(pugi::xml_node node) {
			const std::string shortId = declaredId(node, "chain");
			const std::string label = entryLabel("chain", shortId);
			attributesOnly(node, {"id", "script"}, label);
			reserveScript(node, label);
			ChainEntry	entry{.id = fullId(shortId), .file = source};
			std::size_t index = 0;
			for (const pugi::xml_node stepNode : node.children()) {
				if (stepNode.type() != pugi::node_element) {
					continue;
				}
				const std::string_view element = stepNode.name();
				const std::string	   where = label + " " + stepLabel(index++, element);
				ChainStep			   step;
				if (element == "go") {
					attributesOnly(stepNode, {"query", "to"}, where);
					GoStep	   go;
					const bool hasQuery = !stepNode.attribute("query").empty();
					const bool hasTo = !stepNode.attribute("to").empty();
					if (hasQuery == hasTo) {
						failAt(
							where + " names " + (hasQuery ? "both a query and a target" : "neither a query nor a target") +
							"; give exactly one of query and to"
						);
					} else if (hasQuery) {
						go.query = reference<QueryEntry>(stepNode, "query", where);
					} else if (const std::optional<std::size_t> target = choice(stepNode, "to", where, kStepTargetNames, true)) {
						go.to = static_cast<StepTarget>(*target);
					}
					step.step = std::move(go);
				} else if (element == "do") {
					attributesOnly(stepNode, {"action", "on", "claims", "amount"}, where);
					DoStep doStep{.action = reference<ActionEntry>(stepNode, "action", where)};
					if (const std::optional<std::size_t> on = choice(stepNode, "on", where, kStepTargetNames, false)) {
						doStep.on = static_cast<StepTarget>(*on);
					}
					if (const std::optional<std::size_t> amount = choice(stepNode, "amount", where, kStepAmountNames, false)) {
						doStep.amount = static_cast<StepAmount>(*amount);
					}
					if (!stepNode.attribute("claims").empty()) {
						const std::vector<std::string> claims = splitNames(stepNode.attribute("claims").value());
						if (claims.empty()) {
							refuse(where, stepNode, "claims", "claim kinds (" + joined(kClaimKindNames) + ")");
						}
						for (const std::string& claim : claims) {
							const std::optional<std::size_t> kindIndex = indexIn(kClaimKindNames, claim);
							if (!kindIndex) {
								fail(where, "claims " + inQuotes(claim) + " is not a claim kind (" + joined(kClaimKindNames) + ")");
								continue;
							}
							const auto bit = static_cast<uint8_t>(1U << *kindIndex);
							if ((doStep.claims & bit) != 0) {
								fail(where, "claims names " + inQuotes(claim) + " twice");
							}
							doStep.claims |= bit;
						}
					}
					step.step = std::move(doStep);
				} else if (element == "have") {
					attributesOnly(stepNode, {"item", "capability", "fetch"}, where);
					HaveStep   have;
					const bool hasItem = !stepNode.attribute("item").empty();
					const bool hasCapability = !stepNode.attribute("capability").empty();
					if (hasItem == hasCapability) {
						failAt(
							where + " names " + (hasItem ? "both an item and a capability" : "neither an item nor a capability") +
							"; give exactly one"
						);
					} else if (hasItem) {
						have.item = itemArgument(stepNode, "item", where, false);
					} else {
						have.capability = capability(stepNode, "capability", where, true);
					}
					have.fetch = reference<ChainEntry>(stepNode, "fetch", where);
					step.step = std::move(have);
				} else if (element == "think") {
					attributesOnly(stepNode, {"thought", "to"}, where);
					ThinkStep think{.thought = required(stepNode, "thought", where)};
					if (const std::optional<std::size_t> to = choice(stepNode, "to", where, kRecipientNames, true)) {
						think.to = static_cast<ThoughtRecipient>(*to);
					}
					step.step = std::move(think);
				} else if (element == "need") {
					attributesOnly(stepNode, {"id", "change"}, where);
					NeedStep needStep;
					if (const std::optional<NeedId> id = needName(stepNode, "id", where)) {
						needStep.need = *id;
					}
					needStep.change = number(stepNode, "change", where);
					step.step = needStep;
				} else if (element == "drop" || element == "leave_colony") {
					attributesOnly(stepNode, {}, where);
					step.step = element == "drop" ? decltype(step.step)(DropStep{}) : decltype(step.step)(LeaveColonyStep{});
				} else if (element == "run") {
					attributesOnly(stepNode, {"chain"}, where);
					step.step = RunStep{.chain = reference<ChainEntry>(stepNode, "chain", where)};
				} else {
					failAt(label + ": unknown element <" + std::string(element) + ">");
					continue;
				}
				childrenOnly(stepNode, {"ends"}, where);
				for (const pugi::xml_node ends : stepNode.children("ends")) {
					const std::string endsLabel = where + " <ends>";
					attributesOnly(ends, {"on"}, endsLabel);
					if (const std::optional<std::size_t> event = choice(ends, "on", endsLabel, kEndEventNames, true)) {
						const auto bit = static_cast<uint8_t>(1U << *event);
						if ((step.ends & bit) != 0) {
							fail(endsLabel, "on " + inQuotes(kEndEventNames[*event]) + " is given twice");
						}
						step.ends |= bit;
					}
				}
				entry.steps.push_back(std::move(step));
			}
			if (entry.steps.empty()) {
				failAt(label + " has no step");
			}
			if (shortId.empty()) {
				return std::nullopt;
			}
			return entry;
		}

		// --- FileReader: queries ---------------------------------------------------------------------------

		std::optional<FromRow> FileReader::fromRow(pugi::xml_node node, const std::string& label) {
			static constexpr std::array<std::string_view, 8> kSources = {
				"capability", "kind", "item", "yields", "pile", "box", "place", "goal"
			};
			std::vector<std::string_view> given;
			for (const std::string_view sourceName : kSources) {
				if (!node.attribute(std::string(sourceName).c_str()).empty()) {
					given.push_back(sourceName);
				}
			}
			if (given.size() != 1) {
				failAt(
					label + (given.empty() ? " names no source" : " names more than one source (" + joined(given) + ")") +
					"; give exactly one of " + joined(kSources)
				);
				return std::nullopt;
			}
			const std::string_view sourceName = given.front();
			FromRow				   row;
			if (!node.attribute("fallback").empty()) {
				row.fallback = boolean(node, "fallback", label).value_or(false);
			}
			if (sourceName == "capability") {
				attributesOnly(node, {"capability", "fallback"}, label);
				row.source = CandidateSource::Capability;
				const std::optional<CapabilityType> parsed = capability(node, "capability", label, true);
				if (!parsed) {
					return std::nullopt;
				}
				row.capability = *parsed;
			} else if (sourceName == "kind") {
				attributesOnly(node, {"kind", "fallback"}, label);
				row.source = CandidateSource::Kind;
				row.kind = required(node, "kind", label);
			} else if (sourceName == "item" || sourceName == "yields" || sourceName == "pile" || sourceName == "box") {
				attributesOnly(node, {"item", "yields", "pile", "box", "fallback"}, label);
				row.source = sourceName == "item"	  ? CandidateSource::Item
							 : sourceName == "yields" ? CandidateSource::Yields
							 : sourceName == "pile"	  ? CandidateSource::Pile
													  : CandidateSource::Box;
				const std::optional<ItemArgument> item =
					itemArgument(node, std::string(sourceName).c_str(), label, row.source != CandidateSource::Item);
				if (!item) {
					return std::nullopt;
				}
				row.item = *item;
			} else if (sourceName == "place") {
				static constexpr std::array<std::string_view, 3> kPlaces = {"here", "around", "grid"};
				const std::optional<std::size_t>				 place = choice(node, "place", label, kPlaces, true);
				if (!place) {
					return std::nullopt;
				}
				if (*place == 0) {
					attributesOnly(node, {"place", "fallback"}, label);
					row.source = CandidateSource::PlaceHere;
				} else if (*place == 1) {
					attributesOnly(node, {"place", "radius", "count", "minRadius", "fallback"}, label);
					row.source = CandidateSource::PlaceAround;
					row.radius = boundedNumber(node, "radius", label, 0.0F, std::numeric_limits<float>::max(), true);
					row.count = reader.labeledInteger(label, node, "count", 1, std::numeric_limits<int>::max());
					if (!node.attribute("minRadius").empty()) {
						row.minRadius = boundedNumber(node, "minRadius", label, 0.0F, std::numeric_limits<float>::max());
						if (!std::isnan(row.minRadius) && !std::isnan(row.radius) && row.minRadius >= row.radius) {
							refuse(label, node, "minRadius", "below radius (" + numberText(row.radius) + ")");
						}
					}
				} else {
					attributesOnly(node, {"place", "spacing", "radius", "fallback"}, label);
					row.source = CandidateSource::PlaceGrid;
					row.spacing = boundedNumber(node, "spacing", label, 0.0F, std::numeric_limits<float>::max(), true);
					row.radius = boundedNumber(node, "radius", label, 0.0F, std::numeric_limits<float>::max(), true);
				}
			} else {
				attributesOnly(node, {"goal", "fallback"}, label);
				static constexpr std::array<std::string_view, 2> kGoalSources = {"destination", "spots"};
				const std::optional<std::size_t>				 goal = choice(node, "goal", label, kGoalSources, true);
				if (!goal) {
					return std::nullopt;
				}
				if (kind != FileKind::Work) {
					fail(
						label,
						"goal " + inQuotes(kGoalSources[*goal]) + " reads the goal a work option serves, which only a query in work/ has"
					);
					return std::nullopt;
				}
				row.source = *goal == 0 ? CandidateSource::GoalDestination : CandidateSource::GoalSpots;
			}
			childrenOnly(node, {}, label);
			return row;
		}

		std::optional<QueryEntry> FileReader::query(pugi::xml_node node) {
			const std::string shortId = declaredId(node, "query");
			const std::string label = entryLabel("query", shortId);
			attributesOnly(node, {"id", "pick", "script"}, label);
			reserveScript(node, label);
			QueryEntry										 entry{.id = fullId(shortId), .file = source};
			static constexpr std::array<std::string_view, 2> kPicks = {"best", "weighted"};
			if (const std::optional<std::size_t> pick = choice(node, "pick", label, kPicks, false)) {
				entry.pick = static_cast<QueryPick>(*pick);
			}
			const std::size_t problemsBefore = problems.size() + reader.problemCount();
			std::size_t		  index = 0;
			for (const pugi::xml_node row : node.children()) {
				if (row.type() != pugi::node_element) {
					continue;
				}
				const std::string_view element = row.name();
				const std::string	   where = label + " " + rowLabel(index++, element);
				if (element == "from") {
					if (std::optional<FromRow> from = fromRow(row, where)) {
						entry.from.push_back(std::move(*from));
					}
				} else if (element == "weigh" || element == "exclude" || element == "prefer") {
					FactRow factRow;
					if (element == "weigh") {
						attributesOnly(row, {"fact", "by", "when"}, where);
						factRow.kind = FactRowKind::Weigh;
						factRow.by = number(row, "by", where);
					} else {
						if (element == "exclude") {
							attributesOnly(row, {"fact", "above", "below", "when"}, where);
						} else {
							attributesOnly(row, {"fact", "above", "below"}, where);
						}
						factRow.kind = element == "exclude" ? FactRowKind::Exclude : FactRowKind::Prefer;
						if (!row.attribute("above").empty() && !row.attribute("below").empty()) {
							failAt(where + " gives both above and below; a row draws one line");
						}
						factRow.above = optionalNumber(row, "above", where);
						factRow.below = optionalNumber(row, "below", where);
					}
					if (!row.attribute("when").empty()) {
						factRow.when = required(row, "when", where);
					}
					childrenOnly(row, {}, where);
					if (const std::optional<FactUse> used = fact(row, "fact", where)) {
						factRow.fact = *used;
						entry.rows.push_back(std::move(factRow));
					}
				} else if (element == "scale") {
					attributesOnly(row, {"trait", "fact", "by"}, where);
					childrenOnly(row, {}, where);
					ScaleRow scale{.trait = required(row, "trait", where), .by = number(row, "by", where)};
					if (const std::optional<FactUse> used = fact(row, "fact", where)) {
						scale.fact = *used;
						entry.scales.push_back(std::move(scale));
					}
				} else {
					failAt(label + ": unknown element <" + std::string(element) + ">");
				}
			}
			if (problems.size() + reader.problemCount() == problemsBefore) {
				const uint16_t distance = *factIndex("distance");
				const auto	   bounded = [&](FactRowKind rowKind) {
					return std::ranges::any_of(entry.rows, [&](const FactRow& row) {
						if (row.kind != rowKind || row.fact.fact != distance || !row.when.empty()) {
							return false;
						}
						return rowKind == FactRowKind::Weigh ? row.by < 0.0F : row.above.has_value();
					});
				};
				if (entry.from.empty()) {
					failAt(label + " has no <from> row, so it has no candidates");
				}
				if (entry.pick == QueryPick::Weighted && !bounded(FactRowKind::Exclude)) {
					failAt(label + " picks weighted but excludes no distance above a bound, so its draw has no bound");
				} else if (!bounded(FactRowKind::Weigh) && !bounded(FactRowKind::Exclude)) {
					failAt(
						label + " neither weighs distance by a negative number nor excludes it above a bound, so nothing ends its search"
					);
				}
			}
			if (shortId.empty()) {
				return std::nullopt;
			}
			return entry;
		}

		// --- FileReader: actions ---------------------------------------------------------------------------

		std::optional<EffectRow> FileReader::effect(pugi::xml_node node, const std::string& label) {
			const std::string_view element = node.name();
			childrenOnly(node, {}, label);
			if (element == "restore") {
				attributesOnly(node, {"need", "amount", "from", "scale", "scaleBy", "seconds", "upTo", "who"}, label);
				RestoreEffect restore;
				if (const std::optional<NeedId> need = needName(node, "need", label)) {
					restore.need = *need;
				}
				static constexpr std::array<std::string_view, 2> kWho = {"self", "target"};
				if (const std::optional<std::size_t> who = choice(node, "who", label, kWho, false)) {
					restore.onTarget = *who == 1;
				}
				const bool hasAmount = !node.attribute("amount").empty();
				const bool hasFrom = !node.attribute("from").empty();
				if (hasAmount == hasFrom) {
					failAt(label + " gives " + (hasAmount ? "both amount and from" : "neither amount nor from") + "; give exactly one");
				} else if (hasAmount) {
					restore.amount = number(node, "amount", label);
					if (!node.attribute("scale").empty()) {
						refuse(label, node, "scale", "left out; it scales a from property");
					}
				} else {
					static constexpr std::array<std::string_view, 1> kProperties = {"nutrition"};
					if (choice(node, "from", label, kProperties, true)) {
						restore.from = ConsumedProperty::Nutrition;
					}
					restore.scale = number(node, "scale", label);
				}
				if (!node.attribute("scaleBy").empty()) {
					restore.scaleBy = fact(node, "scaleBy", label);
				}
				const bool hasSeconds = !node.attribute("seconds").empty();
				const bool hasUpTo = !node.attribute("upTo").empty();
				if (hasSeconds != hasUpTo) {
					failAt(
						label + " gives " + (hasSeconds ? "seconds without upTo" : "upTo without seconds") +
						"; an open-ended restore gives both"
					);
				} else if (hasSeconds) {
					if (hasFrom) {
						failAt(label + " gives from with seconds and upTo; an open-ended restore gives an amount per that many seconds");
					}
					restore.seconds = boundedNumber(node, "seconds", label, 0.0F, std::numeric_limits<float>::max(), true);
					restore.upTo = boundedNumber(node, "upTo", label, 0.0F, 100.0F);
				}
				return restore;
			}
			if (element == "consume") {
				attributesOnly(node, {"capability", "count", "seconds", "upTo", "need"}, label);
				ConsumeEffect consume;
				if (const std::optional<CapabilityType> parsed = capability(node, "capability", label, true)) {
					consume.capability = *parsed;
				}
				consume.count = reader.labeledInteger(label, node, "count", 1, std::numeric_limits<int>::max());
				const int given = static_cast<int>(!node.attribute("seconds").empty()) + static_cast<int>(!node.attribute("upTo").empty()) +
								  static_cast<int>(!node.attribute("need").empty());
				if (given != 0 && given != 3) {
					failAt(label + " gives some of seconds, upTo, and need; an open-ended consume gives all three");
				} else if (given == 3) {
					consume.seconds = boundedNumber(node, "seconds", label, 0.0F, std::numeric_limits<float>::max(), true);
					consume.upTo = boundedNumber(node, "upTo", label, 0.0F, 100.0F);
					consume.need = needName(node, "need", label);
				}
				return consume;
			}
			if (element == "take") {
				attributesOnly(node, {"clears"}, label);
				return TakeEffect{.clears = boolean(node, "clears", label).value_or(false)};
			}
			if (element == "work") {
				attributesOnly(node, {"rate", "direction"}, label);
				WorkEffect										 work{.rate = required(node, "rate", label)};
				static constexpr std::array<std::string_view, 2> kDirections = {"up", "down"};
				if (const std::optional<std::size_t> direction = choice(node, "direction", label, kDirections, true)) {
					work.direction = static_cast<WorkDirection>(*direction);
				}
				return work;
			}
			if (element == "spawn") {
				attributesOnly(node, {"def", "into"}, label);
				SpawnEffect spawn{.def = node.attribute("def").value()};
				if (const std::optional<uint32_t> id = definition(node, "def", label)) {
					spawn.defNameId = *id;
				}
				static constexpr std::array<std::string_view, 1> kInto = {"hands"};
				spawn.intoHands = choice(node, "into", label, kInto, false).has_value();
				return spawn;
			}
			if (element == "damage") {
				attributesOnly(node, {"amount"}, label);
				return DamageEffect{.amount = boundedNumber(node, "amount", label, 0.0F, std::numeric_limits<float>::max(), true)};
			}
			attributesOnly(node, {}, label);
			if (element == "put") {
				return PutEffect{};
			}
			if (element == "craft") {
				return CraftEffect{};
			}
			return InstallEffect{};
		}

		std::optional<ActionEntry> FileReader::action(pugi::xml_node node) {
			const std::string shortId = declaredId(node, "action");
			const std::string label = entryLabel("action", shortId);
			attributesOnly(
				node, {"id", "seconds", "workFrom", "skill", "hands", "interruptable", "pays", "curve", "asleep", "on", "script"}, label
			);
			reserveScript(node, label);
			ActionEntry entry{.id = fullId(shortId), .file = source};
			std::size_t index = 0;
			for (const pugi::xml_node effectNode : node.children()) {
				if (effectNode.type() != pugi::node_element) {
					continue;
				}
				static constexpr std::array<std::string_view, 9> kEffects = {
					"restore", "consume", "take", "put", "work", "craft", "spawn", "install", "damage"
				};
				if (!indexIn(kEffects, effectNode.name())) {
					failAt(label + ": unknown element <" + std::string(effectNode.name()) + ">");
					continue;
				}
				if (std::optional<EffectRow> row = effect(
						effectNode, label + " " + std::string("effect ") + std::to_string(++index) + " <" + effectNode.name() + ">"
					)) {
					entry.effects.push_back(std::move(*row));
				}
			}
			if (index == 0) {
				failAt(label + " has no effect");
			}

			const bool openEnded = std::ranges::any_of(entry.effects, [](const EffectRow& row) {
				if (std::holds_alternative<WorkEffect>(row)) {
					return true;
				}
				if (const auto* restore = std::get_if<RestoreEffect>(&row)) {
					return restore->upTo.has_value();
				}
				if (const auto* consume = std::get_if<ConsumeEffect>(&row)) {
					return consume->upTo.has_value();
				}
				return false;
			});
			const bool hasSeconds = !node.attribute("seconds").empty();
			const bool hasWorkFrom = !node.attribute("workFrom").empty();
			if (hasSeconds && hasWorkFrom) {
				failAt(label + " gives both seconds and workFrom; an action has one duration");
			} else if (openEnded && (hasSeconds || hasWorkFrom)) {
				refuse(
					label,
					node,
					hasSeconds ? "seconds" : "workFrom",
					"left out; an open-ended action (a <work> effect or an upTo bound) runs until its bound"
				);
			} else if (hasSeconds) {
				entry.duration = ActionDuration::Seconds;
				entry.seconds = boundedNumber(node, "seconds", label, 0.0F, std::numeric_limits<float>::max(), true);
			} else if (hasWorkFrom) {
				entry.duration = ActionDuration::FromDurability;
				static constexpr std::array<std::string_view, 1> kWorkFrom = {"durability"};
				choice(node, "workFrom", label, kWorkFrom, true);
				entry.skill = required(node, "skill", label);
			} else if (openEnded) {
				entry.duration = ActionDuration::OpenEnded;
			} else {
				failAt(label + " has no duration; give seconds, workFrom with skill, or an open-ended effect");
			}
			if (!hasWorkFrom && !node.attribute("skill").empty()) {
				refuse(label, node, "skill", "left out; it is the skill of a workFrom duration");
			}

			static constexpr std::array<std::string_view, 2> kHands = {"any", "free"};
			if (const std::optional<std::size_t> hands = choice(node, "hands", label, kHands, false)) {
				entry.hands = static_cast<Hands>(*hands);
			}
			if (node.attribute("interruptable").empty()) {
				refuse(label, node, "interruptable", "");
			} else {
				entry.interruptable = boolean(node, "interruptable", label).value_or(false);
			}
			static constexpr std::array<std::string_view, 2> kPays = {"end", "along"};
			if (const std::optional<std::size_t> pays = choice(node, "pays", label, kPays, false)) {
				entry.pays = static_cast<Pays>(*pays);
			}
			if (!node.attribute("curve").empty()) {
				if (entry.pays != Pays::Along) {
					refuse(label, node, "curve", "left out; it shapes an action that pays along");
				} else {
					entry.curve = boundedNumber(node, "curve", label, 0.0F, std::numeric_limits<float>::max(), true);
				}
			}
			entry.asleep = boolean(node, "asleep", label).value_or(false);
			if (!node.attribute("on").empty()) {
				const std::string on = node.attribute("on").value();
				if (on == "loose_item") {
					entry.on = ActionTarget{.kind = ActionTarget::Kind::LooseItem};
				} else if (on == "packaged") {
					entry.on = ActionTarget{.kind = ActionTarget::Kind::Packaged};
				} else if (const std::optional<CapabilityType> named = capabilityFromName(on)) {
					entry.on = ActionTarget{.kind = ActionTarget::Kind::Capability, .capability = *named};
				} else {
					refuse(label, node, "on", "a capability (" + capabilityNames() + "), loose_item, or packaged");
				}
			}
			if (shortId.empty()) {
				return std::nullopt;
			}
			return entry;
		}

		std::optional<ActionEntry> FileReader::actionGroup(pugi::xml_node node) {
			const std::string shortId = declaredId(node, "actionGroup");
			const std::string label = entryLabel("actionGroup", shortId);
			attributesOnly(node, {"id", "script"}, label);
			childrenOnly(node, {}, label);
			reserveScript(node, label);
			ActionEntry entry{.id = fullId(shortId), .file = source, .group = true};
			for (std::string& member : splitNames(node.text().as_string())) {
				entry.members.push_back(EntryReference<ActionEntry>{.written = std::move(member)});
			}
			if (entry.members.empty()) {
				failAt(label + " names no member action");
			}
			if (shortId.empty()) {
				return std::nullopt;
			}
			return entry;
		}

		// --- FileReader: behaviors -------------------------------------------------------------------------

		std::optional<LowMoodEntry> FileReader::lowMood(pugi::xml_node node, const std::string& behaviorLabel) {
			const std::string label = behaviorLabel + " <lowMood>";
			attributesOnly(node, {"rampStart", "rampFull", "base"}, label);
			childrenOnly(node, {"target", "weight", "requires", "effect", "ends", "relief"}, label);
			LowMoodEntry entry;
			entry.rampStart = boundedNumber(node, "rampStart", label, 0.0F, 100.0F);
			entry.rampFull = boundedNumber(node, "rampFull", label, 0.0F, 100.0F);
			if (!std::isnan(entry.rampStart) && !std::isnan(entry.rampFull) && entry.rampFull >= entry.rampStart) {
				refuse(label, node, "rampFull", "below rampStart (" + numberText(entry.rampStart) + ")");
			}
			entry.base = boundedNumber(node, "base", label, 0.0F, std::numeric_limits<float>::max());
			const auto exactlyOne = [&](const char* element, bool isRequired) -> pugi::xml_node {
				const auto		  children = node.children(element);
				const std::size_t count = static_cast<std::size_t>(std::distance(children.begin(), children.end()));
				if (count > 1 || (count == 0 && isRequired)) {
					failAt(
						label + " has " + std::to_string(count) + " <" + element + "> rows; it takes " +
						(isRequired ? "exactly one" : "at most one")
					);
					return {};
				}
				return node.child(element);
			};
			if (const pugi::xml_node target = exactlyOne("target", false)) {
				attributesOnly(target, {"query"}, label + " <target>");
				entry.target = reference<QueryEntry>(target, "query", label + " <target>");
			}
			std::size_t row = 0;
			for (const pugi::xml_node weight : node.children("weight")) {
				const std::string where = label + " " + rowLabel(row++, "weight");
				attributesOnly(weight, {"when", "per", "multiply"}, where);
				WeightRow  weightRow;
				const bool hasWhen = !weight.attribute("when").empty();
				const bool hasPer = !weight.attribute("per").empty();
				if (hasWhen == hasPer) {
					failAt(where + " gives " + (hasWhen ? "both when and per" : "neither when nor per") + "; give exactly one");
				} else if (hasWhen) {
					weightRow.when = required(weight, "when", where);
				} else {
					weightRow.per = required(weight, "per", where);
				}
				weightRow.multiply = boundedNumber(weight, "multiply", where, 0.0F, std::numeric_limits<float>::max());
				entry.weights.push_back(std::move(weightRow));
			}
			row = 0;
			for (const pugi::xml_node requirement : node.children("requires")) {
				const std::string where = label + " " + rowLabel(row++, "requires");
				attributesOnly(requirement, {"when"}, where);
				entry.requirements.push_back(required(requirement, "when", where));
			}
			if (const pugi::xml_node effect = exactlyOne("effect", true)) {
				attributesOnly(effect, {"chain"}, label + " <effect>");
				entry.effect = reference<ChainEntry>(effect, "chain", label + " <effect>");
			}
			if (const pugi::xml_node ends = exactlyOne("ends", true)) {
				attributesOnly(ends, {"after"}, label + " <ends>");
				static constexpr std::array<std::string_view, 1> kAfter = {"task"};
				entry.endsAfterTask = choice(ends, "after", label + " <ends>", kAfter, true).has_value();
			}
			if (const pugi::xml_node relief = exactlyOne("relief", true)) {
				attributesOnly(relief, {"amount"}, label + " <relief>");
				if (const std::optional<std::size_t> amount = choice(relief, "amount", label + " <relief>", kReliefNames, true)) {
					entry.relief = static_cast<Relief>(*amount);
				}
			}
			return entry;
		}

		void FileReader::readBehavior(pugi::xml_node root, std::string_view fullBehaviorId) {
			const std::string_view stem = shortOf(fullBehaviorId);
			const std::string	   label = entryLabel("behavior", root.attribute("id").value());
			attributesOnly(root, {"id", "label", "script"}, label);
			childrenOnly(root, {"need", "chain", "query", "action", "idle", "lowMood"}, label);
			reserveScript(root, label);
			const std::string declared = required(root, "id", "<behavior>");
			if (!declared.empty() && declared != stem) {
				refuse(label, root, "id", "the file's name, " + inQuotes(stem));
			}
			BehaviorEntry behavior{.id = std::string(fullBehaviorId), .file = source, .label = root.attribute("label").value()};
			if (!root.attribute("label").empty() && behavior.label.empty()) {
				refuse(label, root, "label", "a label");
			}
			std::set<std::string> needIds;
			std::size_t			  idleRow = 0;
			for (const pugi::xml_node child : root.children()) {
				if (child.type() != pugi::node_element) {
					continue;
				}
				const std::string_view element = child.name();
				if (element == "need") {
					std::optional<NeedEntry> need = this->need(child);
					if (need) {
						const std::string id = child.attribute("id").value();
						if (!needIds.insert(id).second) {
							failAt(label + " <need id=" + inQuotes(id) + "> is given twice in one behavior");
						}
						need->behavior = EntryHandle<BehaviorEntry>{0};
						behavior.needs.push_back(EntryHandle<NeedEntry>{static_cast<uint32_t>(entries.needs.size())});
						entries.needs.push_back(std::move(*need));
					}
				} else if (element == "chain") {
					if (std::optional<ChainEntry> parsed = chain(child)) {
						entries.chains.push_back(std::move(*parsed));
					}
				} else if (element == "query") {
					if (std::optional<QueryEntry> parsed = query(child)) {
						entries.queries.push_back(std::move(*parsed));
					}
				} else if (element == "action") {
					if (std::optional<ActionEntry> parsed = action(child)) {
						entries.actions.push_back(std::move(*parsed));
					}
				} else if (element == "idle") {
					const std::string where = label + " " + rowLabel(idleRow++, "idle");
					attributesOnly(child, {"chain", "when"}, where);
					childrenOnly(child, {}, where);
					IdleRow row{.chain = reference<ChainEntry>(child, "chain", where)};
					if (!child.attribute("when").empty()) {
						row.when = required(child, "when", where);
					}
					behavior.idle.push_back(std::move(row));
				} else if (element == "lowMood") {
					if (behavior.lowMood) {
						failAt(label + " has more than one <lowMood>; a low-mood behavior holds one");
						continue;
					}
					behavior.lowMood = lowMood(child, label);
				}
			}
			entries.behaviors.push_back(std::move(behavior));
		}

		// --- FileReader: the shared folders ----------------------------------------------------------------

		void FileReader::readActions(pugi::xml_node root) {
			attributesOnly(root, {}, "<actions>");
			childrenOnly(root, {"action", "actionGroup"}, "<actions>");
			for (const pugi::xml_node child : root.children()) {
				std::optional<ActionEntry> parsed;
				if (std::string_view(child.name()) == "action") {
					parsed = action(child);
				} else if (std::string_view(child.name()) == "actionGroup") {
					parsed = actionGroup(child);
				}
				if (parsed) {
					entries.actions.push_back(std::move(*parsed));
				}
			}
		}

		std::optional<CategoryEntry> FileReader::category(pugi::xml_node node) {
			const std::string shortId = declaredId(node, "category");
			const std::string label = entryLabel("category", shortId);
			attributesOnly(node, {"id", "label", "order", "canDisable", "script"}, label);
			childrenOnly(node, {}, label);
			reserveScript(node, label);
			CategoryEntry entry{.id = shortId, .file = source, .label = required(node, "label", label)};
			entry.order = reader.labeledInteger(label, node, "order", 0, std::numeric_limits<int>::max());
			entry.canDisable = boolean(node, "canDisable", label).value_or(true);
			if (shortId.empty()) {
				return std::nullopt;
			}
			return entry;
		}

		std::optional<WorkEntry> FileReader::workEntry(pugi::xml_node node) {
			const std::string shortId = declaredId(node, "work");
			const std::string label = entryLabel("work", shortId);
			attributesOnly(node, {"id", "goal", "need", "category", "rung", "gathers", "source", "script"}, label);
			childrenOnly(node, {"source", "chain"}, label);
			reserveScript(node, label);
			WorkEntry entry{.id = shortId, .file = source};
			if (const std::optional<std::size_t> goal = choice(node, "goal", label, kWorkGoalNames, true)) {
				entry.goal = static_cast<WorkGoal>(*goal);
			}
			if (entry.goal == WorkGoal::Care) {
				entry.need = needName(node, "need", label);
			} else if (!node.attribute("need").empty()) {
				refuse(label, node, "need", "left out; it names the need of a care goal");
			}
			entry.category = reference<CategoryEntry>(node, "category", label);
			const std::string rung = required(node, "rung", label);
			if (rung == kReservedRung) {
				refuse(label, node, "rung", "a rung other than survival, which is reserved");
			} else if (const std::optional<std::size_t> position = indexIn(loader.rungs(), rung); position) {
				entry.rung = static_cast<uint8_t>(*position);
			} else if (!rung.empty()) {
				refuse(label, node, "rung", "a rung of the ladder (" + joined(loader.rungs()) + ")");
			}
			entry.gathers = boolean(node, "gathers", label).value_or(false);
			const auto sources = node.children("source");
			const auto sourceCount = std::distance(sources.begin(), sources.end());
			if (!node.attribute("source").empty()) {
				static constexpr std::array<std::string_view, 1> kCarried = {"carried"};
				entry.sourceCarried = choice(node, "source", label, kCarried, true).has_value();
				if (sourceCount > 0) {
					failAt(label + " gives both source=\"carried\" and a <source> row; give one");
				}
			} else if (sourceCount > 1) {
				failAt(label + " has more than one <source> row");
			} else if (sourceCount == 1) {
				const pugi::xml_node row = node.child("source");
				attributesOnly(row, {"query"}, label + " <source>");
				entry.source = reference<QueryEntry>(row, "query", label + " <source>");
			}
			const auto chains = node.children("chain");
			if (std::distance(chains.begin(), chains.end()) != 1) {
				failAt(
					label + " has " + std::to_string(std::distance(chains.begin(), chains.end())) + " <chain> rows; it names exactly one"
				);
			} else {
				attributesOnly(node.child("chain"), {"id"}, label + " <chain>");
				entry.chain = reference<ChainEntry>(node.child("chain"), "id", label + " <chain>");
			}
			if (shortId.empty()) {
				return std::nullopt;
			}
			return entry;
		}

		void FileReader::readWork(pugi::xml_node root) {
			attributesOnly(root, {}, "<colonyWork>");
			childrenOnly(root, {"category", "work", "chain", "query", "action"}, "<colonyWork>");
			for (const pugi::xml_node child : root.children()) {
				const std::string_view element = child.name();
				if (element == "category") {
					if (std::optional<CategoryEntry> parsed = category(child)) {
						entries.categories.push_back(std::move(*parsed));
					}
				} else if (element == "work") {
					if (std::optional<WorkEntry> parsed = workEntry(child)) {
						entries.work.push_back(std::move(*parsed));
					}
				} else if (element == "chain") {
					if (std::optional<ChainEntry> parsed = chain(child)) {
						entries.chains.push_back(std::move(*parsed));
					}
				} else if (element == "query") {
					if (std::optional<QueryEntry> parsed = query(child)) {
						entries.queries.push_back(std::move(*parsed));
					}
				} else if (element == "action") {
					if (std::optional<ActionEntry> parsed = action(child)) {
						entries.actions.push_back(std::move(*parsed));
					}
				}
			}
		}

		std::optional<OrderEntry> FileReader::order(pugi::xml_node node) {
			const std::string shortId = declaredId(node, "order");
			const std::string label = entryLabel("order", shortId);
			attributesOnly(node, {"id", "label", "on", "chain", "script"}, label);
			childrenOnly(node, {}, label);
			reserveScript(node, label);
			OrderEntry		  entry{.id = shortId, .file = source, .label = required(node, "label", label)};
			const std::string on = required(node, "on", label);
			if (on == "ground") {
				entry.on = OrderTarget{.kind = OrderTarget::Kind::Ground};
			} else if (const std::optional<CapabilityType> named = capabilityFromName(on)) {
				entry.on = OrderTarget{.kind = OrderTarget::Kind::Capability, .capability = *named};
			} else if (!on.empty()) {
				refuse(label, node, "on", "a capability (" + capabilityNames() + ") or ground");
			}
			entry.chain = reference<ChainEntry>(node, "chain", label);
			if (shortId.empty()) {
				return std::nullopt;
			}
			return entry;
		}

		void FileReader::readOrders(pugi::xml_node root) {
			attributesOnly(root, {}, "<orders>");
			childrenOnly(root, {"order", "chain"}, "<orders>");
			for (const pugi::xml_node child : root.children()) {
				if (std::string_view(child.name()) == "order") {
					if (std::optional<OrderEntry> parsed = order(child)) {
						entries.orders.push_back(std::move(*parsed));
					}
				} else if (std::string_view(child.name()) == "chain") {
					if (std::optional<ChainEntry> parsed = chain(child)) {
						entries.chains.push_back(std::move(*parsed));
					}
				}
			}
		}

		void FileReader::readFacts(pugi::xml_node root) {
			attributesOnly(root, {}, "<facts>");
			childrenOnly(root, {"fact"}, "<facts>");
			std::set<uint16_t> given;
			for (const pugi::xml_node node : root.children("fact")) {
				const std::string id = node.attribute("id").value();
				const std::string label = "<fact id=" + inQuotes(id) + ">";
				childrenOnly(node, {}, label);
				if (node.attribute("id").empty()) {
					refuse("<fact>", node, "id", "");
					continue;
				}
				const std::optional<uint16_t> index = factIndex(id);
				if (!index) {
					refuse("<fact>", node, "id", "a fact the sim provides");
					continue;
				}
				const FactDeclaration& declaration = kFacts[*index];
				if (declaration.tuning.empty()) {
					failAt(label + " reads no tuning value, so it has no row here");
					continue;
				}
				const std::string tuning(declaration.tuning);
				attributesOnly(node, {"id", declaration.tuning}, label);
				if (!given.insert(*index).second) {
					failAt(label + " is given twice in one file");
					continue;
				}
				const float value = tuning == "ground"
										? boundedNumber(node, tuning.c_str(), label, 0.0F, 1.0F)
										: boundedNumber(node, tuning.c_str(), label, 0.0F, std::numeric_limits<float>::max(), true);
				entries.factValues.emplace_back(*index, value);
			}
		}

		// --- FileReader: creatures -------------------------------------------------------------------------

		std::optional<CreatureNeedRow> FileReader::creatureNeed(pugi::xml_node node, const std::string& label) {
			attributesOnly(node, {"id", "from", "label", "seek", "urgent", "critical"}, label);
			childrenOnly(node, {"drains", "setBy", "satisfiedBy"}, label);
			CreatureNeedRow row{.source = source};
			if (const std::optional<NeedId> id = needName(node, "id", label)) {
				row.id = node.attribute("id").value();
			}
			row.from = node.attribute("from").value();
			if (!node.attribute("from").empty() && row.from.empty()) {
				refuse(label, node, "from", "a behavior id");
			}
			if (!node.attribute("label").empty()) {
				row.label = required(node, "label", label);
			}
			for (const auto& [attribute, value] :
				 {std::pair{"seek", &row.seek}, std::pair{"urgent", &row.urgent}, std::pair{"critical", &row.critical}}) {
				if (!node.attribute(attribute).empty()) {
					*value = boundedNumber(node, attribute, label, 0.0F, 100.0F);
				}
			}
			row.move = needMove(node, label, false);
			if (node.child("satisfiedBy")) {
				row.satisfiedBy = satisfiedByRows(node, label);
			}
			if (row.id.empty()) {
				return std::nullopt;
			}
			return row;
		}

		void FileReader::readCreatureBlock(pugi::xml_node block, bool primary, CreatureRows& rows) {
			const std::string label = "<creature>";
			if (primary) {
				attributesOnly(block, {"mood"}, label);
				if (block.attribute("mood").empty()) {
					refuse(label, block, "mood", "");
				} else {
					rows.mood = boolean(block, "mood", label).value_or(false);
				}
			} else {
				attributesOnly(block, {}, label);
			}
			childrenOnly(block, {"behavior", "need", "moodCost"}, label);
			std::size_t index = 0;
			for (const pugi::xml_node child : block.children()) {
				if (child.type() != pugi::node_element) {
					continue;
				}
				const std::string_view element = child.name();
				const std::string	   where = label + " " + rowLabel(index, element);
				if (element == "behavior") {
					attributesOnly(child, {"id"}, where);
					childrenOnly(child, {}, where);
					const std::string id = required(child, "id", where);
					if (!id.empty()) {
						rows.includes.push_back(CreatureInclude{.source = source, .row = index, .id = id});
					}
				} else if (element == "need") {
					if (std::optional<CreatureNeedRow> row =
							creatureNeed(child, "<need id=" + inQuotes(child.attribute("id").value()) + ">")) {
						rows.needs.push_back(std::move(*row));
					}
				} else if (element == "moodCost") {
					attributesOnly(child, {"need", "cost"}, where);
					childrenOnly(child, {}, where);
					CreatureCostRow cost{
						.source = source, .cost = boundedNumber(child, "cost", where, 0.0F, std::numeric_limits<float>::max())
					};
					if (needName(child, "need", where)) {
						cost.need = child.attribute("need").value();
						rows.costs.push_back(std::move(cost));
					}
				}
				++index;
			}
		}

		// --- Loader: reading the folders -------------------------------------------------------------------

		void Loader::append(FileReader& file) {
			std::vector<std::string> problems = file.takeProblems();
			if (!problems.empty()) {
				result.errors.insert(result.errors.end(), problems.begin(), problems.end());
				return;
			}
			EntryLibrary& library = result.library;
			FileEntries&  entries = file.entries;
			const auto	  needOffset = static_cast<uint32_t>(library.needs.size());
			const auto	  behaviorOffset = static_cast<uint32_t>(library.behaviors.size());
			for (NeedEntry& need : entries.needs) {
				need.behavior.index += behaviorOffset;
				definedNeeds.insert(need.id.index);
				library.needs.push_back(std::move(need));
			}
			for (BehaviorEntry& behavior : entries.behaviors) {
				for (EntryHandle<NeedEntry>& need : behavior.needs) {
					need.index += needOffset;
				}
				library.behaviors.push_back(std::move(behavior));
			}
			const auto move = [](auto& from, auto& to) {
				std::move(from.begin(), from.end(), std::back_inserter(to));
			};
			move(entries.chains, library.chains);
			move(entries.queries, library.queries);
			move(entries.actions, library.actions);
			move(entries.categories, library.categories);
			move(entries.work, library.work);
			move(entries.orders, library.orders);
		}

		void Loader::readSharedFolder(std::string_view folderName, const char* rootName, FileKind kind) {
			const std::string			prefix = "[" + std::string(folderName) + "] ";
			const std::filesystem::path folder = sources.configRoot / folderName;
			const ConfigFiles			found = configFilesIn(folder);
			if (found.error) {
				fail(prefix + *found.error);
				return;
			}
			for (const std::filesystem::path& file : found.files) {
				const std::string		 source = prefix + configFileName(file, folder);
				pugi::xml_document		 document;
				FileReader				 reader(*this, source, "", kind);
				std::vector<std::string> rootErrors;
				const pugi::xml_node	 root = readRootElement(document, file, rootName, source, rootErrors);
				if (!root) {
					result.errors.insert(result.errors.end(), rootErrors.begin(), rootErrors.end());
					continue;
				}
				if (kind == FileKind::Facts) {
					reader.readFacts(root);
					std::vector<std::pair<uint16_t, float>> values = reader.entries.factValues;
					const std::size_t						errorsBefore = result.errors.size();
					append(reader);
					if (result.errors.size() == errorsBefore) {
						for (const auto& [fact, value] : values) {
							if (!factSources[fact].empty()) {
								fail(source + ": <fact id=" + inQuotes(kFacts[fact].name) + "> is also given in " + factSources[fact]);
								continue;
							}
							factSources[fact] = source;
							result.library.factTuning[fact] = value;
						}
					}
					continue;
				}
				if (kind == FileKind::Actions) {
					reader.readActions(root);
				} else if (kind == FileKind::Work) {
					reader.readWork(root);
				} else {
					reader.readOrders(root);
				}
				append(reader);
			}
		}

		void Loader::readBehaviorFile(const std::filesystem::path& file, const std::string& source, const std::string& fullBehaviorId) {
			pugi::xml_document		 document;
			std::vector<std::string> rootErrors;
			const pugi::xml_node	 root = readRootElement(document, file, "behavior", source, rootErrors);
			if (!root) {
				result.errors.insert(result.errors.end(), rootErrors.begin(), rootErrors.end());
				return;
			}
			FileReader reader(*this, source, std::string(folderOf(fullBehaviorId)), FileKind::Behavior);
			reader.readBehavior(root, fullBehaviorId);
			append(reader);
		}

		void Loader::readBehaviorsFolder() {
			const std::filesystem::path folder = sources.configRoot / "behaviors";
			const ConfigFiles			found = configFilesIn(folder);
			if (found.error) {
				fail("[behaviors] " + *found.error);
				return;
			}
			for (const std::filesystem::path& file : found.files) {
				const std::string name = configFileName(file, folder);
				const std::string source = "[behaviors] " + name;
				const std::string fullBehaviorId = name.substr(0, name.size() - file.extension().string().size());
				if (folderOf(fullBehaviorId).empty()) {
					fail(source + ": a behavior file sits in a folder under behaviors/, the folder its id starts with");
					continue;
				}
				readBehaviorFile(file, source, fullBehaviorId);
			}
		}

		void Loader::readCreatures() {
			for (const CreatureConfigFiles& creature : sources.creatures) {
				assert(creature.errors.empty() && "loadEntries takes creature folders with no structural errors");
				std::filesystem::path folder = creature.folder.lexically_normal();
				if (!folder.has_filename() && folder.has_relative_path()) {
					folder = folder.parent_path();
				}
				const std::string name = folder.filename().string();
				CreatureRows	  rows{.name = name, .primarySource = "[creatures] " + name + "/" + name + ".xml"};

				pugi::xml_document			 definitions;
				const pugi::xml_parse_result parsed = definitions.load_file((folder / (name + ".xml")).string().c_str());
				if (!parsed) {
					fail(rows.primarySource + ": not readable XML: " + parsed.description());
					continue;
				}
				FileReader				 primary(*this, rows.primarySource, name, FileKind::CreatureBlock);
				pugi::xml_node			 base;
				std::vector<std::string> placement;
				for (const pugi::xml_node definition : definitions.child("AssetDefinitions").children("AssetDef")) {
					const std::string defName = ConfigReader::describe(definition, "AssetDef");
					const bool		  isBase = std::string_view(definition.child_value("defName")) == name;
					const auto		  blocks = definition.children("creature");
					const auto		  count = std::distance(blocks.begin(), blocks.end());
					if (isBase) {
						base = definition;
						if (count != 1) {
							placement.push_back(
								rows.primarySource + ": " + defName +
								(count == 0 ? " has no <creature> block; the base definition of a creature folder carries one"
											: " has more than one <creature> block")
							);
						}
					} else if (count > 0) {
						placement.push_back(
							rows.primarySource + ": " + defName + " has a <creature> block, which only the base definition (" + name +
							") carries"
						);
					}
				}
				if (!base) {
					placement.push_back(
						rows.primarySource + ": no <AssetDef> has the folder's name, " + inQuotes(name) +
						", so the creature has no base definition"
					);
				}
				if (!placement.empty()) {
					result.errors.insert(result.errors.end(), placement.begin(), placement.end());
					continue;
				}
				primary.readCreatureBlock(base.child("creature"), true, rows);
				std::vector<std::string> problems = primary.takeProblems();
				result.errors.insert(result.errors.end(), problems.begin(), problems.end());

				for (const std::filesystem::path& file : creature.creature) {
					const std::string		 source = "[creatures] " + name + "/" + file.filename().string();
					pugi::xml_document		 document;
					std::vector<std::string> rootErrors;
					const pugi::xml_node	 root = readRootElement(document, file, "creature", source, rootErrors);
					if (!root) {
						result.errors.insert(result.errors.end(), rootErrors.begin(), rootErrors.end());
						continue;
					}
					FileReader	 reader(*this, source, name, FileKind::CreatureBlock);
					CreatureRows fileRows;
					reader.readCreatureBlock(root, false, fileRows);
					std::vector<std::string> fileProblems = reader.takeProblems();
					if (!fileProblems.empty()) {
						result.errors.insert(result.errors.end(), fileProblems.begin(), fileProblems.end());
						continue;
					}
					std::ranges::move(fileRows.includes, std::back_inserter(rows.includes));
					std::ranges::move(fileRows.needs, std::back_inserter(rows.needs));
					std::ranges::move(fileRows.costs, std::back_inserter(rows.costs));
				}
				for (const std::filesystem::path& file : creature.behaviors) {
					const std::string stem = file.stem().string();
					const std::string fullBehaviorId = name + "/" + stem;
					readBehaviorFile(file, "[creatures] " + name + "/" + file.filename().string(), fullBehaviorId);
					rows.ownBehaviors.push_back(fullBehaviorId);
				}
				creatureRows.push_back(std::move(rows));
			}
		}

		void Loader::checkFactTuning() {
			for (std::size_t index = 0; index < kFacts.size(); ++index) {
				if (!kFacts[index].tuning.empty() && factSources[index].empty()) {
					fail(
						"[facts] <fact id=" + inQuotes(kFacts[index].name) + "> " + std::string(kFacts[index].tuning) +
						" is given by no file in facts/"
					);
				}
			}
		}

		// --- Loader: ids -----------------------------------------------------------------------------------

		template <typename Entry>
		void Loader::registerEach(
			std::vector<Entry>&	   entries,
			EntryLibrary::IdIndex& index,
			std::string_view	   element,
			std::string_view	   typeName
		) {
			for (uint32_t position = 0; position < entries.size(); ++position) {
				const Entry& entry = entries[position];
				const auto [found, added] = index.try_emplace(entry.id, position);
				if (!added) {
					fail(
						entry.file + ": " + entryLabel(element, entry.id) + " " + std::string(typeName) + " " + inQuotes(entry.id) +
						" is defined twice, first in " + entries[found->second].file
					);
				}
			}
			for (const Entry& entry : entries) {
				if (folderOf(entry.id).empty()) {
					continue;
				}
				const auto topLevel = index.find(shortOf(entry.id));
				if (topLevel != index.end()) {
					warn(
						entry.file + ": " + entryLabel(element, entry.id) + " " + std::string(typeName) + " " + inQuotes(entry.id) +
						" hides the top-level " + std::string(typeName) + " " + inQuotes(shortOf(entry.id)) + " (" +
						entries[topLevel->second].file + ") inside " + std::string(folderOf(entry.id)) + "/"
					);
				}
			}
		}

		void Loader::registerIds() {
			EntryLibrary& library = result.library;
			registerEach(library.behaviors, library.behaviorIds, "behavior", "behavior");
			registerEach(library.chains, library.chainIds, "chain", "chain");
			registerEach(library.queries, library.queryIds, "query", "query");
			for (uint32_t position = 0; position < library.actions.size(); ++position) {
				const ActionEntry& entry = library.actions[position];
				const char*		   element = entry.group ? "actionGroup" : "action";
				const auto [found, added] = library.actionIds.try_emplace(entry.id, position);
				if (!added) {
					fail(
						entry.file + ": " + entryLabel(element, entry.id) + " action " + inQuotes(entry.id) +
						" is defined twice, first in " + library.actions[found->second].file
					);
				}
			}
			for (const ActionEntry& entry : library.actions) {
				if (folderOf(entry.id).empty()) {
					continue;
				}
				const auto topLevel = library.actionIds.find(shortOf(entry.id));
				if (topLevel != library.actionIds.end()) {
					warn(
						entry.file + ": " + entryLabel(entry.group ? "actionGroup" : "action", entry.id) + " action " + inQuotes(entry.id) +
						" hides the top-level action " + inQuotes(shortOf(entry.id)) + " (" + library.actions[topLevel->second].file +
						") inside " + std::string(folderOf(entry.id)) + "/"
					);
				}
			}
			registerEach(library.categories, library.categoryIds, "category", "category");
			registerEach(library.work, library.workIds, "work", "work entry");
			registerEach(library.orders, library.orderIds, "order", "order");
		}

		// --- Loader: references ----------------------------------------------------------------------------

		template <typename Entry>
		bool Loader::resolveReference(
			EntryReference<Entry>&		 reference,
			const EntryLibrary::IdIndex& index,
			std::string_view			 inFolder,
			const std::string&			 where,
			const char*					 attribute,
			std::string_view			 typeName
		) {
			if (reference.written.empty()) {
				return false;
			}
			auto found = index.end();
			if (reference.written.find('/') != std::string::npos) {
				found = index.find(reference.written);
			} else {
				if (!inFolder.empty()) {
					found = index.find(std::string(inFolder) + "/" + reference.written);
				}
				if (found == index.end()) {
					found = index.find(reference.written);
				}
			}
			if (found == index.end()) {
				fail(where + " " + attribute + " " + inQuotes(reference.written) + " names no " + std::string(typeName));
				return false;
			}
			reference.handle = EntryHandle<Entry>{found->second};
			return true;
		}

		void Loader::checkNeedDefined(NeedId need, const std::string& where, const char* attribute) {
			if (need.index < result.library.needIds.size() && !definedNeeds.contains(need.index)) {
				fail(
					where + " " + attribute + " " + inQuotes(result.library.needIds[need.index]) +
					" names no need of any behavior or creature"
				);
			}
		}

		void Loader::resolveSatisfiedBy(std::vector<SatisfiedBy>& rows, std::string_view inFolder, const std::string& where) {
			const EntryLibrary& library = result.library;
			for (std::size_t index = 0; index < rows.size(); ++index) {
				const std::string rowWhere = where + " " + rowLabel(index, "satisfiedBy");
				if (rows[index].chain) {
					resolveReference(*rows[index].chain, library.chainIds, inFolder, rowWhere, "chain", "chain");
				}
				if (rows[index].work) {
					resolveReference(*rows[index].work, library.workIds, inFolder, rowWhere, "work", "work entry");
				}
			}
		}

		void Loader::resolve() {
			EntryLibrary& library = result.library;
			for (ChainEntry& chain : library.chains) {
				const std::string_view inFolder = folderOf(chain.id);
				const std::string	   label = chain.file + ": " + entryLabel("chain", chain.id);
				for (std::size_t index = 0; index < chain.steps.size(); ++index) {
					std::visit(
						[&](auto& step) {
							using Step = std::decay_t<decltype(step)>;
							if constexpr (std::is_same_v<Step, GoStep>) {
								if (step.query) {
									resolveReference(
										*step.query, library.queryIds, inFolder, label + " " + stepLabel(index, "go"), "query", "query"
									);
								}
							} else if constexpr (std::is_same_v<Step, DoStep>) {
								resolveReference(
									step.action, library.actionIds, inFolder, label + " " + stepLabel(index, "do"), "action", "action"
								);
							} else if constexpr (std::is_same_v<Step, HaveStep>) {
								resolveReference(
									step.fetch, library.chainIds, inFolder, label + " " + stepLabel(index, "have"), "fetch", "chain"
								);
							} else if constexpr (std::is_same_v<Step, RunStep>) {
								resolveReference(
									step.chain, library.chainIds, inFolder, label + " " + stepLabel(index, "run"), "chain", "chain"
								);
							} else if constexpr (std::is_same_v<Step, NeedStep>) {
								checkNeedDefined(step.need, label + " " + stepLabel(index, "need"), "id");
							}
						},
						chain.steps[index].step
					);
				}
			}
			for (ActionEntry& action : library.actions) {
				const std::string label = action.file + ": " + entryLabel(action.group ? "actionGroup" : "action", action.id);
				if (!action.group) {
					for (std::size_t index = 0; index < action.effects.size(); ++index) {
						const std::string where = label + " effect " + std::to_string(index + 1);
						if (const auto* restore = std::get_if<RestoreEffect>(&action.effects[index])) {
							checkNeedDefined(restore->need, where + " <restore>", "need");
						} else if (const auto* consume = std::get_if<ConsumeEffect>(&action.effects[index]); consume && consume->need) {
							checkNeedDefined(*consume->need, where + " <consume>", "need");
						}
					}
					continue;
				}
				std::vector<const ActionEntry*> members;
				for (EntryReference<ActionEntry>& member : action.members) {
					if (!resolveReference(member, library.actionIds, folderOf(action.id), label, "member", "action")) {
						continue;
					}
					const ActionEntry& resolved = library.actions[member.handle.index];
					if (resolved.group) {
						fail(label + " member " + inQuotes(member.written) + " is an action group; a group's members are actions");
					} else if (!resolved.on) {
						fail(label + " member " + inQuotes(member.written) + " has no on, so no target can choose it");
					} else {
						for (const ActionEntry* earlier : members) {
							if (sameTarget(*earlier->on, *resolved.on)) {
								fail(
									label + " members " + inQuotes(shortOf(earlier->id)) + " and " + inQuotes(member.written) +
									" both work on " + std::string(targetName(*resolved.on))
								);
							}
						}
						members.push_back(&resolved);
					}
				}
			}
			for (NeedEntry& need : library.needs) {
				const std::string_view inFolder = folderOf(library.behaviors[need.behavior.index].id);
				resolveSatisfiedBy(need.satisfiedBy, inFolder, need.file + ": <need id=" + inQuotes(library.needIds[need.id.index]) + ">");
			}
			for (BehaviorEntry& behavior : library.behaviors) {
				const std::string_view inFolder = folderOf(behavior.id);
				const std::string	   label = behavior.file + ": " + entryLabel("behavior", behavior.id);
				for (std::size_t index = 0; index < behavior.idle.size(); ++index) {
					resolveReference(
						behavior.idle[index].chain, library.chainIds, inFolder, label + " " + rowLabel(index, "idle"), "chain", "chain"
					);
				}
				if (behavior.lowMood) {
					if (behavior.lowMood->target) {
						resolveReference(
							*behavior.lowMood->target, library.queryIds, inFolder, label + " <lowMood> <target>", "query", "query"
						);
					}
					resolveReference(behavior.lowMood->effect, library.chainIds, inFolder, label + " <lowMood> <effect>", "chain", "chain");
				}
			}
			for (WorkEntry& work : library.work) {
				const std::string label = work.file + ": " + entryLabel("work", work.id);
				resolveReference(work.category, library.categoryIds, "", label, "category", "category");
				if (work.source) {
					resolveReference(*work.source, library.queryIds, "", label + " <source>", "query", "query");
				}
				resolveReference(work.chain, library.chainIds, "", label + " <chain>", "id", "chain");
				if (work.need) {
					checkNeedDefined(*work.need, label, "need");
				}
			}
			for (OrderEntry& order : library.orders) {
				resolveReference(order.chain, library.chainIds, "", order.file + ": " + entryLabel("order", order.id), "chain", "chain");
			}
		}

		// --- Loader: chain rules ---------------------------------------------------------------------------

		void Loader::checkChains() {
			const EntryLibrary& library = result.library;
			const auto			childChains = [&](const ChainEntry& chain) {
				 std::vector<uint32_t> children;
				 for (const ChainStep& step : chain.steps) {
					 if (const auto* have = std::get_if<HaveStep>(&step.step); have && have->fetch.handle.valid()) {
						 children.push_back(have->fetch.handle.index);
					 } else if (const auto* run = std::get_if<RunStep>(&step.step); run && run->chain.handle.valid()) {
						 children.push_back(run->chain.handle.index);
					 }
				 }
				 return children;
			};

			// A chain that runs itself, through `run` or a `have` fetch, never ends.
			enum class Visit : uint8_t { New, Open, Done };
			std::vector<Visit>	  visits(library.chains.size(), Visit::New);
			std::vector<uint32_t> path;
			std::vector<bool>	  inCycle(library.chains.size(), false);
			const auto			  visit = [&](auto& self, uint32_t chain) -> void {
				   visits[chain] = Visit::Open;
				   path.push_back(chain);
				   for (const uint32_t child : childChains(library.chains[chain])) {
					   if (visits[child] == Visit::Open) {
						   std::string loop;
						   const auto  start = std::ranges::find(path, child);
						   for (auto at = start; at != path.end(); ++at) {
							   loop += inQuotes(library.chains[*at].id) + " -> ";
							   inCycle[*at] = true;
						   }
						   fail(
							   library.chains[child].file + ": " + entryLabel("chain", library.chains[child].id) + " runs itself: " + loop +
							   inQuotes(library.chains[child].id)
						   );
					   } else if (visits[child] == Visit::New) {
						   self(self, child);
					   }
				   }
				   path.pop_back();
				   visits[chain] = Visit::Done;
			};
			for (uint32_t chain = 0; chain < library.chains.size(); ++chain) {
				if (visits[chain] == Visit::New) {
					visit(visit, chain);
				}
			}

			// The claims a chain names in its own steps and in every chain it may run: four on a task, of which the
			// two that follow the goods (inbound, in-flight) are what a suspended record keeps.
			constexpr uint8_t kGoodsClaims =
				(1U << static_cast<uint8_t>(ClaimKind::Inbound)) | (1U << static_cast<uint8_t>(ClaimKind::InFlight));
			std::vector<std::optional<std::pair<int, int>>> totals(library.chains.size());
			const auto										claimTotal = [&](auto& self, uint32_t chain) -> std::pair<int, int> {
				 if (totals[chain]) {
					 return *totals[chain];
				 }
				 std::pair<int, int> total{0, 0};
				 for (const ChainStep& step : library.chains[chain].steps) {
					 if (const auto* doStep = std::get_if<DoStep>(&step.step)) {
						 total.first += std::popcount(doStep->claims);
						 total.second += std::popcount(static_cast<uint8_t>(doStep->claims & kGoodsClaims));
					 }
				 }
				 for (const uint32_t child : childChains(library.chains[chain])) {
					 const std::pair<int, int> childTotal = self(self, child);
					 total.first += childTotal.first;
					 total.second += childTotal.second;
				 }
				 totals[chain] = total;
				 return total;
			};
			for (uint32_t chain = 0; chain < library.chains.size(); ++chain) {
				const ChainEntry& entry = library.chains[chain];
				const std::string label = entry.file + ": " + entryLabel("chain", entry.id);
				if (!inCycle[chain]) {
					const std::pair<int, int> total = claimTotal(claimTotal, chain);
					if (total.first > 4) {
						fail(label + " names " + std::to_string(total.first) + " claims with the chains it runs; a task holds four");
					}
					if (total.second > 2) {
						fail(
							label + " names " + std::to_string(total.second) +
							" inbound and in_flight claims with the chains it runs; a suspended record holds two"
						);
					}
				}

				for (std::size_t index = 0; index < entry.steps.size(); ++index) {
					const auto* doStep = std::get_if<DoStep>(&entry.steps[index].step);
					if (doStep == nullptr || !doStep->action.handle.valid()) {
						continue;
					}
					const ActionEntry&				action = library.actions[doStep->action.handle.index];
					std::vector<const ActionEntry*> performed;
					if (action.group) {
						for (const EntryReference<ActionEntry>& member : action.members) {
							if (member.handle.valid()) {
								performed.push_back(&library.actions[member.handle.index]);
							}
						}
					} else {
						performed.push_back(&action);
					}
					const std::string where = label + " " + stepLabel(index, "do");
					if (doStep->amount) {
						const bool movesGoods = std::ranges::any_of(performed, [](const ActionEntry* candidate) {
							return std::ranges::any_of(candidate->effects, [](const EffectRow& row) {
								return std::holds_alternative<TakeEffect>(row) || std::holds_alternative<PutEffect>(row);
							});
						});
						if (!movesGoods) {
							fail(
								where + " amount is for a step whose action takes or puts; " + inQuotes(doStep->action.written) +
								" does neither"
							);
						}
					}
					if (!action.group || index == 0) {
						continue;
					}
					// The group's members must cover every target the query of the go step before it can return.
					const GoStep* go = nullptr;
					for (std::size_t back = index; back-- > 0;) {
						if ((go = std::get_if<GoStep>(&entry.steps[back].step)) != nullptr) {
							break;
						}
					}
					if (go == nullptr || !go->query || !go->query->handle.valid()) {
						continue;
					}
					std::vector<std::string_view> uncovered;
					for (const FromRow& from : library.queries[go->query->handle.index].from) {
						std::optional<ActionTarget> target;
						switch (from.source) {
							case CandidateSource::Yields:
								target = ActionTarget{.kind = ActionTarget::Kind::Capability, .capability = CapabilityType::Harvestable};
								break;
							case CandidateSource::Pile:
								target = ActionTarget{.kind = ActionTarget::Kind::LooseItem};
								break;
							case CandidateSource::Box:
								target = ActionTarget{.kind = ActionTarget::Kind::Capability, .capability = CapabilityType::Storage};
								break;
							case CandidateSource::Capability:
								target = ActionTarget{.kind = ActionTarget::Kind::Capability, .capability = from.capability};
								break;
							default:
								break;
						}
						if (!target) {
							continue;
						}
						const bool covered = std::ranges::any_of(performed, [&](const ActionEntry* member) {
							return member->on && sameTarget(*member->on, *target);
						});
						if (!covered && std::ranges::find(uncovered, targetName(*target)) == uncovered.end()) {
							uncovered.push_back(targetName(*target));
						}
					}
					if (!uncovered.empty()) {
						fail(
							where + " action group " + inQuotes(doStep->action.written) + " has no member for " + joined(uncovered) +
							", which query " + inQuotes(go->query->written) + " can return"
						);
					}
				}
			}
		}

		void Loader::warnUnstarted() {
			const EntryLibrary&	  library = result.library;
			std::vector<bool>	  started(library.chains.size(), false);
			std::vector<uint32_t> pending;
			const auto			  start = [&](const EntryReference<ChainEntry>& chain) {
				   if (chain.handle.valid() && !started[chain.handle.index]) {
					   started[chain.handle.index] = true;
					   pending.push_back(chain.handle.index);
				   }
			};
			const auto startRows = [&](const std::vector<SatisfiedBy>& rows) {
				for (const SatisfiedBy& row : rows) {
					if (row.chain) {
						start(*row.chain);
					}
				}
			};
			for (const NeedEntry& need : library.needs) {
				startRows(need.satisfiedBy);
			}
			for (const BehaviorEntry& behavior : library.behaviors) {
				for (const IdleRow& idle : behavior.idle) {
					start(idle.chain);
				}
				if (behavior.lowMood) {
					start(behavior.lowMood->effect);
				}
			}
			for (const WorkEntry& work : library.work) {
				start(work.chain);
			}
			for (const OrderEntry& order : library.orders) {
				start(order.chain);
			}
			for (const CreatureEntry& creature : library.creatures) {
				for (const CreatureNeed& need : creature.needs) {
					startRows(need.satisfiedBy);
				}
			}
			while (!pending.empty()) {
				const uint32_t chain = pending.back();
				pending.pop_back();
				for (const ChainStep& step : library.chains[chain].steps) {
					if (const auto* have = std::get_if<HaveStep>(&step.step)) {
						start(have->fetch);
					} else if (const auto* run = std::get_if<RunStep>(&step.step)) {
						start(run->chain);
					}
				}
			}
			for (uint32_t chain = 0; chain < library.chains.size(); ++chain) {
				if (!started[chain]) {
					const ChainEntry& entry = library.chains[chain];
					warn(
						entry.file + ": " + entryLabel("chain", entry.id) + " chain " + inQuotes(entry.id) +
						" is started by nothing: no need, idle row, work entry, order, or episode names it"
					);
				}
			}
		}

		// --- Loader: creatures -----------------------------------------------------------------------------

		void Loader::resolveCreatures() {
			EntryLibrary& library = result.library;
			for (CreatureRows& rows : creatureRows) {
				CreatureEntry	  creature{.name = rows.name, .mood = rows.mood};
				const std::size_t errorsBefore = result.errors.size();

				// The creature's order: the primary block's includes, then its <creature> files', then its own
				// behavior files not already placed by a row.
				std::map<uint32_t, std::string> placedBy;
				for (CreatureInclude& include : rows.includes) {
					const std::string where = include.source + ": <creature> " + rowLabel(include.row, "behavior");
					const auto		  found = library.behaviorIds.find(include.id);
					if (found == library.behaviorIds.end()) {
						fail(where + " id " + inQuotes(include.id) + " names no behavior");
						continue;
					}
					const auto [placed, added] = placedBy.try_emplace(found->second, where);
					if (!added) {
						fail(where + " id " + inQuotes(include.id) + " includes a behavior already included at " + placed->second);
						continue;
					}
					creature.behaviors.push_back(EntryHandle<BehaviorEntry>{found->second});
				}
				for (const std::string& own : rows.ownBehaviors) {
					const auto found = library.behaviorIds.find(own);
					if (found != library.behaviorIds.end() && !placedBy.contains(found->second)) {
						placedBy.emplace(found->second, library.behaviors[found->second].file);
						creature.behaviors.push_back(EntryHandle<BehaviorEntry>{found->second});
					}
				}

				// Each need id the included behaviors bring, with every behavior that brings it, in order.
				std::vector<std::pair<std::string, std::vector<uint32_t>>> brought;
				for (const EntryHandle<BehaviorEntry> behavior : creature.behaviors) {
					for (const EntryHandle<NeedEntry> need : library.behaviors[behavior.index].needs) {
						const std::string& id = library.needIds[library.needs[need.index].id.index];
						auto			   found = std::ranges::find(brought, id, &std::pair<std::string, std::vector<uint32_t>>::first);
						if (found == brought.end()) {
							brought.emplace_back(id, std::vector<uint32_t>{});
							found = std::prev(brought.end());
						}
						found->second.push_back(need.index);
					}
				}
				std::vector<std::string> needOrder;
				for (const auto& [id, needs] : brought) {
					needOrder.push_back(id);
				}
				for (const CreatureNeedRow& row : rows.needs) {
					if (std::ranges::find(needOrder, row.id) == needOrder.end()) {
						needOrder.push_back(row.id);
					}
				}

				for (const std::string& id : needOrder) {
					const auto broughtBy = std::ranges::find(brought, id, &std::pair<std::string, std::vector<uint32_t>>::first);
					std::vector<const CreatureNeedRow*> needRows;
					for (const CreatureNeedRow& row : rows.needs) {
						if (row.id == id) {
							needRows.push_back(&row);
						}
					}
					const std::string needLabel = "<need id=" + inQuotes(id) + ">";

					// Settling: which behavior's entry the need takes when several bring it.
					const CreatureNeedRow* settledBy = nullptr;
					for (const CreatureNeedRow* row : needRows) {
						if (row->from.empty()) {
							continue;
						}
						if (settledBy != nullptr) {
							fail(row->source + ": " + needLabel + " from is also given in " + settledBy->source);
							continue;
						}
						settledBy = row;
					}
					CreatureNeed	 need{.id = *library.findNeed(id)};
					const NeedEntry* base = nullptr;
					if (broughtBy != brought.end()) {
						if (settledBy != nullptr) {
							const auto from = library.behaviorIds.find(settledBy->from);
							const auto match = std::ranges::find_if(broughtBy->second, [&](uint32_t candidate) {
								return from != library.behaviorIds.end() && library.needs[candidate].behavior.index == from->second;
							});
							if (match == broughtBy->second.end()) {
								fail(
									settledBy->source + ": " + needLabel + " from " + inQuotes(settledBy->from) +
									" names no included behavior that brings the need"
								);
							} else {
								base = &library.needs[*match];
							}
						} else if (broughtBy->second.size() > 1) {
							std::vector<std::string> names;
							for (const uint32_t candidate : broughtBy->second) {
								names.push_back(inQuotes(library.behaviors[library.needs[candidate].behavior.index].id));
							}
							fail(
								rows.primarySource + ": <creature> need " + inQuotes(id) + " comes from more than one included behavior (" +
								joined(names) + "); settle it with <need id=" + inQuotes(id) + " from=\"...\"/>"
							);
						} else {
							base = &library.needs[broughtBy->second.front()];
						}
					} else if (settledBy != nullptr) {
						fail(
							settledBy->source + ": " + needLabel + " from " + inQuotes(settledBy->from) +
							" settles a need no included behavior brings"
						);
					}

					std::optional<std::string> label;
					std::optional<float>	   seek;
					std::optional<float>	   urgent;
					std::optional<float>	   critical;
					std::optional<NeedMove>	   move;
					std::vector<SatisfiedBy>   satisfiedBy;
					if (base != nullptr) {
						label = base->label;
						seek = base->seek;
						urgent = base->urgent;
						critical = base->critical;
						move = base->move;
						satisfiedBy = base->satisfiedBy;
						need.behavior = base->behavior;
					}
					// Each value is set by one row at most; two rows setting it are an error naming both files.
					std::map<std::string, std::string, std::less<>> setBy;
					const auto										set = [&](const CreatureNeedRow& row, std::string_view value) {
						 const auto [found, added] = setBy.try_emplace(std::string(value), row.source);
						 if (!added) {
							 fail(row.source + ": " + needLabel + " " + std::string(value) + " is also set in " + found->second);
						 }
					};
					for (const CreatureNeedRow* row : needRows) {
						if (row->label) {
							set(*row, "label");
							label = row->label;
						}
						if (row->seek) {
							set(*row, "seek");
							seek = row->seek;
						}
						if (row->urgent) {
							set(*row, "urgent");
							urgent = row->urgent;
						}
						if (row->critical) {
							set(*row, "critical");
							critical = row->critical;
						}
						if (row->move) {
							set(*row, "how the level moves (<drains> or <setBy>)");
							move = row->move;
						}
						if (row->satisfiedBy) {
							if (broughtBy != brought.end()) {
								fail(
									row->source + ": " + needLabel +
									" has <satisfiedBy> rows; what meets an included need comes from its behavior"
								);
								continue;
							}
							set(*row, "<satisfiedBy>");
							satisfiedBy = *row->satisfiedBy;
						}
					}
					const std::string where = rows.primarySource + ": <creature> need " + inQuotes(id);
					if (!label || !seek || !urgent || !critical || !move) {
						if (base == nullptr && broughtBy == brought.end()) {
							std::vector<std::string_view> missing;
							if (!label) {
								missing.emplace_back("label");
							}
							if (!seek) {
								missing.emplace_back("seek");
							}
							if (!urgent) {
								missing.emplace_back("urgent");
							}
							if (!critical) {
								missing.emplace_back("critical");
							}
							if (!move) {
								missing.emplace_back("<drains> or <setBy>");
							}
							fail(
								where + " is no included behavior's need, so the creature's rows give all of it, and they give no " +
								joined(missing)
							);
						}
						continue;
					}
					if (*urgent >= *seek) {
						fail(where + " urgent must be below seek (" + numberText(*seek) + "), got " + inQuotes(numberText(*urgent)));
					}
					if (*critical >= *urgent) {
						fail(
							where + " critical must be below urgent (" + numberText(*urgent) + "), got " + inQuotes(numberText(*critical))
						);
					}
					need.label = *label;
					need.seek = *seek;
					need.urgent = *urgent;
					need.critical = *critical;
					need.move = *move;
					need.satisfiedBy = std::move(satisfiedBy);
					if (broughtBy == brought.end()) {
						resolveSatisfiedBy(need.satisfiedBy, rows.name, rows.primarySource + ": <creature> " + needLabel);
						definedNeeds.insert(need.id.index);
					}
					creature.needs.push_back(std::move(need));
				}

				// Mood costs: one for every need of a creature with mood, none for one without.
				std::map<std::string, std::string, std::less<>> costSources;
				for (const CreatureCostRow& cost : rows.costs) {
					const std::string where = cost.source + ": <moodCost need=" + inQuotes(cost.need) + ">";
					if (!rows.mood) {
						fail(where + " gives a mood cost, and the creature has no mood (mood=\"false\")");
						continue;
					}
					const auto [found, added] = costSources.try_emplace(cost.need, cost.source);
					if (!added) {
						fail(where + " is also given in " + found->second);
						continue;
					}
					const auto need = std::ranges::find_if(creature.needs, [&](const CreatureNeed& candidate) {
						return library.needIds[candidate.id.index] == cost.need;
					});
					if (need == creature.needs.end()) {
						fail(where + " names a need the creature does not have");
						continue;
					}
					need->moodCost = cost.cost;
				}
				if (rows.mood) {
					for (const CreatureNeed& need : creature.needs) {
						if (!need.moodCost) {
							fail(
								rows.primarySource + ": <creature> need " + inQuotes(library.needIds[need.id.index]) +
								" has no <moodCost>; a creature with mood gives every need a cost"
							);
						}
					}
				}
				if (result.errors.size() == errorsBefore) {
					library.creatures.push_back(std::move(creature));
				}
			}
		}

		EntryLoad Loader::run() {
			readBehaviorsFolder();
			readSharedFolder("actions", "actions", FileKind::Actions);
			readSharedFolder("work", "colonyWork", FileKind::Work);
			readSharedFolder("orders", "orders", FileKind::Orders);
			readSharedFolder("facts", "facts", FileKind::Facts);
			readCreatures();
			if (result.errors.empty()) {
				checkFactTuning();
			}
			if (result.errors.empty()) {
				registerIds();
				resolve();
				resolveCreatures();
				checkChains();
			}
			if (!result.errors.empty()) {
				result.warnings.clear();
				result.library = EntryLibrary{};
				return std::move(result);
			}
			warnUnstarted();
			const EntryLibrary& library = result.library;
			LOG_INFO(
				Engine,
				"Loaded %zu behaviors, %zu needs, %zu chains, %zu queries, %zu actions, %zu work entries, %zu orders, %zu creatures",
				library.behaviors.size(),
				library.needs.size(),
				library.chains.size(),
				library.queries.size(),
				library.actions.size(),
				library.work.size(),
				library.orders.size(),
				library.creatures.size()
			);
			return std::move(result);
		}

	} // namespace

	EntryLoad loadEntries(const EntrySources& sources) {
		return Loader(sources).run();
	}

} // namespace assets
