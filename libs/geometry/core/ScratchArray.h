#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>

namespace geometry {

	// Working storage indexed by slot (visit stamps, per-slot marks) that a pass reads only
	// where it wrote this pass: growing hands back a new, zeroed array and drops the old
	// contents, so it never copies them. The zeroed memory comes from calloc, which takes
	// a large block as fresh pages zeroed when first touched, so growing to any size costs
	// no pass over it. An all-zero element must be a valid one (a stamp no pass uses).
	template <typename T> class ScratchArray {
		static_assert(std::is_trivially_copyable_v<T>, "ScratchArray: elements are zeroed as bytes");

	  public:
		ScratchArray() = default;
		// A copy is fresh scratch of the same size.
		ScratchArray(const ScratchArray& other) { reallocate(other.count); }
		ScratchArray& operator=(const ScratchArray& other) {
			if (this != &other) {
				reallocate(other.count);
			}
			return *this;
		}
		ScratchArray(ScratchArray&&) noexcept			 = default;
		ScratchArray& operator=(ScratchArray&&) noexcept = default;

		std::size_t size() const { return count; }
		T&			operator[](std::size_t i) { return data[i]; }
		const T&	operator[](std::size_t i) const { return data[i]; }

		// At least n elements; growing zeroes every one, old contents included.
		void growTo(std::size_t n) {
			if (n > count) {
				reallocate(std::max(n, count + count / 2));
			}
		}

		// Every element back to zero.
		void zero() {
			if (count > 0) {
				std::memset(static_cast<void*>(data.get()), 0, count * sizeof(T));
			}
		}

	  private:
		void reallocate(std::size_t n) {
			data.reset(n > 0 ? static_cast<T*>(std::calloc(n, sizeof(T))) : nullptr);
			if (n > 0 && data == nullptr) {
				throw std::bad_alloc();
			}
			count = n;
		}

		struct Free {
			void operator()(T* p) const { std::free(p); }
		};
		std::unique_ptr<T[], Free> data;
		std::size_t				   count = 0;
	};

} // namespace geometry
