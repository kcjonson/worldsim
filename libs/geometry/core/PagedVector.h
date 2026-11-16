#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace geometry {

	// A growable array in fixed pages of kPageSize elements: growing allocates the pages it
	// needs and never moves an element, so a slot table of any size grows at a cost
	// proportional to the growth, and a reference to an element stays valid while it lives.
	// Shrinking keeps the pages for the next growth.
	template <typename T> class PagedVector {
	  public:
		static constexpr int		 kPageShift = 12;
		static constexpr std::size_t kPageSize	= std::size_t{1} << kPageShift;
		static constexpr std::size_t kPageMask	= kPageSize - 1;

		PagedVector() = default;
		PagedVector(const PagedVector& other) { *this = other; }
		PagedVector(PagedVector&&) noexcept = default;
		PagedVector& operator=(PagedVector&&) noexcept = default;
		PagedVector& operator=(const PagedVector& other) {
			if (this == &other) {
				return *this;
			}
			pages.clear();
			table.clear();
			count = 0;
			reserve(other.count);
			for (std::size_t i = 0; i < other.count; ++i) {
				(*this)[i] = other[i];
			}
			count = other.count;
			return *this;
		}

		std::size_t size() const { return count; }
		bool		empty() const { return count == 0; }

		T&		 operator[](std::size_t i) { return table[i >> kPageShift][i & kPageMask]; }
		const T& operator[](std::size_t i) const { return table[i >> kPageShift][i & kPageMask]; }

		// The pages in order, for a reader that indexes them itself; valid until the next
		// growth adds a page.
		T* const* pageTable() const { return table.data(); }
		T&		 back() { return (*this)[count - 1]; }
		const T& back() const { return (*this)[count - 1]; }

		void push_back(const T& value) {
			reserve(count + 1);
			(*this)[count++] = value;
		}

		template <typename... Args> T& emplace_back(Args&&... args) {
			reserve(count + 1);
			T& slot = (*this)[count++];
			slot	= T(std::forward<Args>(args)...);
			return slot;
		}

		// New elements take `value`.
		void resize(std::size_t n, const T& value = T{}) {
			reserve(n);
			for (std::size_t i = count; i < n; ++i) {
				(*this)[i] = value;
			}
			count = n;
		}

		void assign(std::size_t n, const T& value) {
			reserve(n);
			for (std::size_t i = 0; i < n; ++i) {
				(*this)[i] = value;
			}
			count = n;
		}

		void clear() { count = 0; }
		void pop_back() { --count; }

		bool operator==(const PagedVector& other) const {
			if (count != other.count) {
				return false;
			}
			for (std::size_t i = 0; i < count; ++i) {
				if (!((*this)[i] == other[i])) {
					return false;
				}
			}
			return true;
		}

		// Every element in order.
		template <typename Visit> void forEach(Visit visit) const {
			for (std::size_t i = 0; i < count; ++i) {
				visit((*this)[i]);
			}
		}

		class ConstIterator {
		  public:
			ConstIterator(const PagedVector* owner, std::size_t at) : owner(owner), at(at) {}
			const T&	   operator*() const { return (*owner)[at]; }
			ConstIterator& operator++() {
				++at;
				return *this;
			}
			bool operator==(const ConstIterator& other) const { return at == other.at; }
			bool operator!=(const ConstIterator& other) const { return at != other.at; }

		  private:
			const PagedVector* owner;
			std::size_t		   at;
		};
		ConstIterator begin() const { return {this, 0}; }
		ConstIterator end() const { return {this, count}; }

	  private:
		void reserve(std::size_t n) {
			while (pages.size() * kPageSize < n) {
				pages.push_back(std::make_unique<T[]>(kPageSize));
				table.push_back(pages.back().get());
			}
		}

		std::vector<std::unique_ptr<T[]>> pages;
		std::vector<T*>					  table; // pages[k].get()
		std::size_t						  count = 0;
	};

} // namespace geometry
