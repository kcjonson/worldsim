#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace geometry {

	// An open-addressed map (linear probing) that grows without a pause: when the table
	// passes half full, a table twice the size takes over, and every later
	// operation moves a few slots of the old one across until it is empty. A lookup reads
	// both tables meanwhile. So no insert pays to rehash the whole map, and the cost of
	// growth is spread over the operations that caused it.
	//
	// Hash is a callable from Key to std::uint64_t; Key compares with ==.
	template <typename Key, typename Value, typename Hash> class IncrementalHashMap {
	  public:
		std::size_t size() const { return count; }

		Value* find(const Key& key) {
			if (Slot* s = findIn(table, key)) {
				return &s->value;
			}
			if (Slot* s = findIn(old, key)) {
				return &s->value;
			}
			return nullptr;
		}
		const Value* find(const Key& key) const { return const_cast<IncrementalHashMap*>(this)->find(key); }

		// The key must be absent.
		void insert(const Key& key, const Value& value) {
			if (2 * (live + 1) > table.size()) {
				startGrowth();
			}
			place(table, key, value);
			++live;
			++count;
			drain();
		}

		bool erase(const Key& key) {
			bool found = eraseFrom(key);
			if (!found && !old.empty()) {
				// In the table being drained a slot is only marked: shifting would move an
				// entry behind the drain cursor.
				if (Slot* s = findIn(old, key)) {
					s->state = State::Gone;
					--oldLive;
					found = true;
				}
			}
			if (found) {
				--count;
				drain();
			}
			return found;
		}

		void clear() {
			table.clear();
			old.clear();
			live	= 0;
			oldLive = 0;
			count	= 0;
			cursor	= 0;
		}

	  private:
		enum class State : std::uint8_t { Empty, Full, Gone };
		struct Slot {
			Key	  key{};
			Value value{};
			State state = State::Empty;
		};
		// Slots in memory the allocator hands back zeroed (an all-zero slot is empty): a
		// large table comes as fresh pages, zeroed when first touched, so allocating one
		// costs no pass over it.
		class Slots {
		  public:
			Slots() = default;
			Slots(const Slots& other) { *this = other; }
			Slots(Slots&& other) noexcept : data(std::move(other.data)), n(std::exchange(other.n, 0)) {}
			Slots& operator=(Slots&& other) noexcept {
				data = std::move(other.data);
				n	 = std::exchange(other.n, 0);
				return *this;
			}
			Slots& operator=(const Slots& other) {
				if (this != &other) {
					zeroed(other.n);
					if (n > 0) {
						std::memcpy(static_cast<void*>(data.get()), other.data.get(), n * sizeof(Slot));
					}
				}
				return *this;
			}
			void zeroed(std::size_t count) {
				data.reset(count > 0 ? static_cast<Slot*>(std::calloc(count, sizeof(Slot))) : nullptr);
				if (count > 0 && data == nullptr) {
					throw std::bad_alloc();
				}
				n = count;
			}
			void		clear() { zeroed(0); }
			std::size_t size() const { return n; }
			bool		empty() const { return n == 0; }
			Slot&		operator[](std::size_t i) { return data[i]; }
			const Slot& operator[](std::size_t i) const { return data[i]; }

		  private:
			struct Free {
				void operator()(Slot* p) const { std::free(p); }
			};
			std::unique_ptr<Slot[], Free> data;
			std::size_t					  n = 0;
		};
		static_assert(std::is_trivially_copyable_v<Key> && std::is_trivially_copyable_v<Value>, "IncrementalHashMap: slots are copied as bytes");

		// Draining old (n slots, at most n / 2 full) takes n / 8 operations, which add at
		// most n / 8 entries to a table of 2n slots holding n / 2 moved: it stays under
		// half full until old is gone.
		static constexpr std::size_t kDrainPerOperation = 8;

		std::size_t home(const Slots& t, const Key& key) const { return static_cast<std::size_t>(hash(key)) & (t.size() - 1); }

		Slot* findIn(Slots& t, const Key& key) {
			if (t.empty()) {
				return nullptr;
			}
			for (std::size_t i = home(t, key);; i = (i + 1) & (t.size() - 1)) {
				Slot& s = t[i];
				if (s.state == State::Empty) {
					return nullptr;
				}
				if (s.state == State::Full && s.key == key) {
					return &s;
				}
			}
		}

		void place(Slots& t, const Key& key, const Value& value) {
			std::size_t i = home(t, key);
			while (t[i].state == State::Full) {
				i = (i + 1) & (t.size() - 1);
			}
			t[i] = {key, value, State::Full};
		}

		// Backward-shift deletion in the live table keeps every probe run unbroken.
		bool eraseFrom(const Key& key) {
			if (table.empty()) {
				return false;
			}
			const std::size_t mask = table.size() - 1;
			std::size_t		  i	   = home(table, key);
			for (;; i = (i + 1) & mask) {
				if (table[i].state == State::Empty) {
					return false;
				}
				if (table[i].key == key) {
					break;
				}
			}
			for (std::size_t j = (i + 1) & mask; table[j].state == State::Full; j = (j + 1) & mask) {
				if (((j - home(table, table[j].key)) & mask) >= ((j - i) & mask)) {
					table[i] = table[j];
					i		 = j;
				}
			}
			table[i] = Slot{};
			--live;
			return true;
		}

		void startGrowth() {
			// A growth still draining finishes first.
			while (!old.empty()) {
				drainSome(old.size());
			}
			old		= std::move(table);
			oldLive = live;
			live	= 0;
			cursor	= 0;
			table.zeroed(old.empty() ? 64 : 2 * old.size());
			if (oldLive == 0) {
				old.clear();
			}
		}

		void drain() {
			if (!old.empty()) {
				drainSome(kDrainPerOperation);
			}
		}

		void drainSome(std::size_t slots) {
			for (std::size_t n = 0; n < slots && cursor < old.size(); ++n, ++cursor) {
				Slot& s = old[cursor];
				if (s.state == State::Full) {
					place(table, s.key, s.value);
					s.state = State::Gone; // moved: lookups must not find it here once erased there
					++live;
					--oldLive;
				}
			}
			if (cursor >= old.size() || oldLive == 0) {
				old.clear();
				cursor = 0;
			}
		}

		Slots table;
		Slots old; // the table being drained, empty when none is
		std::size_t		  live	  = 0; // full slots in table
		std::size_t		  oldLive = 0; // full slots in old
		std::size_t		  count	  = 0;
		std::size_t		  cursor  = 0; // old's next slot to move
		[[no_unique_address]] Hash hash;
	};

} // namespace geometry
