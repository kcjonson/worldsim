#include "ThoughtPruning.h"

#include "systems/TimeSystem.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>
#include <tuple>

namespace ecs {

	namespace {

		enum class Fate : uint8_t { Kept, Prunable, Removed };

		auto subjectKey(const ThoughtSubject& subject) {
			return std::tuple(subject.kind, subject.entity, subject.definition, subject.place.x, subject.place.y);
		}

		auto repeatKey(const Thought& thought) { return std::tuple(thought.eventType, subjectKey(thought.subject), thought.feeling); }

		/// Keeps the first of the thoughts in scratch.order that share a key. The record is in first-arrival
		/// order and never loses a first, so the lowest index with a key is the first thought ever with it.
		template <typename Key>
		void keepFirsts(const std::vector<Thought>& record, ThoughtPruneScratch& scratch, Key key) {
			std::sort(scratch.order.begin(), scratch.order.end(), [&](uint32_t left, uint32_t right) {
				return std::tuple(key(record[left]), left) < std::tuple(key(record[right]), right);
			});
			for (std::size_t position = 0; position < scratch.order.size(); ++position) {
				const uint32_t index = scratch.order[position];
				if (position == 0 || key(record[scratch.order[position - 1]]) != key(record[index])) {
					scratch.fates[index] = static_cast<uint8_t>(Fate::Kept);
				}
			}
		}

		void absorb(Thought& survivor, const Thought& repeat) {
			survivor.count += repeat.count;
			if (repeat.lastRepeatedAt > survivor.lastRepeatedAt) {
				survivor.lastRepeatedAt = repeat.lastRepeatedAt;
				survivor.lastRepeatedClock = repeat.lastRepeatedClock;
			}
			if (repeat.endedAt.has_value() && (!survivor.endedAt.has_value() || *repeat.endedAt > *survivor.endedAt)) {
				survivor.endedAt = repeat.endedAt;
			}
			survivor.merged = true;
		}

	} // namespace

	double thoughtLastMoment(const Thought& thought) {
		return std::max(thought.lastRepeatedAt, thought.endedAt.value_or(thought.lastRepeatedAt));
	}

	double thoughtPruneScore(const Thought& thought, const ThoughtKeeping& keeping, double now) {
		assert(thought.importance != ThoughtImportance::Major && "a major thought is never pruned");
		const double importance = thought.importance == ThoughtImportance::Trivial ? keeping.trivialWeight : keeping.normalWeight;
		const double effect = thought.moodEffect.has_value() ? std::abs(static_cast<double>(thought.moodEffect->amount)) : 0.0;
		const double ageGameDays = (now - thoughtLastMoment(thought)) / kSimulationSecondsPerGameDay;
		return importance * (1.0 + effect) * (1.0 + static_cast<double>(thought.count)) / ageGameDays;
	}

	bool pruneThoughts(Thoughts& thoughts, const ThoughtKeeping& keeping, double now, ThoughtPruneScratch& scratch) {
		std::vector<Thought>& record = thoughts.record;
		if (record.size() < keeping.pruneStartCount) {
			return false;
		}
		const auto	   size = static_cast<uint32_t>(record.size());
		const double   recentFrom = now - keeping.recentSimulationSeconds;
		std::vector<uint8_t>& fates = scratch.fates;
		fates.assign(size, static_cast<uint8_t>(Fate::Prunable));
		for (uint32_t index = 0; index < size; ++index) {
			const Thought& thought = record[index];
			if (isHeld(thought) || thought.importance == ThoughtImportance::Major || thoughtLastMoment(thought) >= recentFrom) {
				fates[index] = static_cast<uint8_t>(Fate::Kept);
			}
		}

		scratch.order.resize(size);
		std::iota(scratch.order.begin(), scratch.order.end(), 0U);
		keepFirsts(record, scratch, [](const Thought& thought) { return thought.eventType; });
		scratch.order.clear();
		for (uint32_t index = 0; index < size; ++index) {
			if (record[index].subject.kind != ThoughtSubjectKind::Nothing) {
				scratch.order.push_back(index);
			}
		}
		keepFirsts(record, scratch, [](const Thought& thought) { return subjectKey(thought.subject); });

		// Pass 1: merge repeats, the run whose first thought is oldest first, each absorbing its thoughts
		// oldest first, until the record is back at the stop size.
		uint32_t remaining = size;
		scratch.order.clear();
		for (uint32_t index = 0; index < size; ++index) {
			if (fates[index] == static_cast<uint8_t>(Fate::Prunable)) {
				scratch.order.push_back(index);
			}
		}
		std::sort(scratch.order.begin(), scratch.order.end(), [&](uint32_t left, uint32_t right) {
			return std::tuple(repeatKey(record[left]), left) < std::tuple(repeatKey(record[right]), right);
		});
		scratch.runs.clear();
		for (uint32_t begin = 0; begin < scratch.order.size();) {
			uint32_t end = begin + 1;
			while (end < scratch.order.size() && repeatKey(record[scratch.order[end]]) == repeatKey(record[scratch.order[begin]])) {
				++end;
			}
			if (end - begin > 1) {
				scratch.runs.push_back({.begin = begin, .end = end});
			}
			begin = end;
		}
		std::sort(scratch.runs.begin(), scratch.runs.end(), [&](const ThoughtPruneScratch::Run& left, const ThoughtPruneScratch::Run& right) {
			return scratch.order[left.begin] < scratch.order[right.begin];
		});
		for (const ThoughtPruneScratch::Run& run : scratch.runs) {
			Thought& survivor = record[scratch.order[run.begin]];
			for (uint32_t position = run.begin + 1; position < run.end && remaining > keeping.pruneStopCount; ++position) {
				const uint32_t index = scratch.order[position];
				absorb(survivor, record[index]);
				fates[index] = static_cast<uint8_t>(Fate::Removed);
				--remaining;
			}
		}

		// Pass 2: drop the lowest scores, the oldest first on a tie.
		if (remaining > keeping.pruneStopCount) {
			scratch.scored.clear();
			for (uint32_t index = 0; index < size; ++index) {
				if (fates[index] == static_cast<uint8_t>(Fate::Prunable)) {
					scratch.scored.push_back({.score = thoughtPruneScore(record[index], keeping, now), .index = index});
				}
			}
			std::sort(scratch.scored.begin(), scratch.scored.end(), [&](const ThoughtPruneScratch::Scored& left, const ThoughtPruneScratch::Scored& right) {
				return std::tuple(left.score, record[left.index].arrivedAt, left.index) <
					   std::tuple(right.score, record[right.index].arrivedAt, right.index);
			});
			const std::size_t dropped = std::min<std::size_t>(remaining - keeping.pruneStopCount, scratch.scored.size());
			for (std::size_t position = 0; position < dropped; ++position) {
				fates[scratch.scored[position].index] = static_cast<uint8_t>(Fate::Removed);
			}
			remaining -= static_cast<uint32_t>(dropped);
		}

		if (remaining == size) {
			return false;
		}
		uint32_t written = 0;
		for (uint32_t index = 0; index < size; ++index) {
			if (fates[index] != static_cast<uint8_t>(Fate::Removed)) {
				if (written != index) {
					record[written] = std::move(record[index]);
				}
				++written;
			}
		}
		record.erase(record.begin() + static_cast<std::ptrdiff_t>(written), record.end());
		++thoughts.revision;
		return true;
	}

} // namespace ecs
