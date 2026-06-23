#pragma once

// Indexed binary min-heap of vertices in lexicographic (s, then t) order.
//
// Stands in for libtess2's priorityq.c. The sweep needs four things from it:
// build from the initial vertex set, peek/extract the minimum, insert new
// vertices mid-sweep (intersection Steiner points), and delete a specific
// still-queued vertex by handle (when two vertices merge). A plain
// std::priority_queue can't delete by handle, so this is a small indexed heap:
// each insertion returns a stable handle that survives sift operations.

#include "Mesh.h"

#include <cstddef>
#include <vector>

namespace renderer::tess {

	class PriorityQueue {
	  public:
		static constexpr int kInvalidHandle = -1;

		// Insert v and return a stable handle usable with remove().
		int insert(Vertex* v);
		// Remove and return the minimum, or null if empty.
		Vertex* extractMin();
		// Peek the minimum without removing, or null if empty.
		Vertex* minimum() const;
		// Delete the element previously inserted under this handle (no-op if already gone).
		void remove(int handle);

		bool   empty() const { return heap.empty(); }
		size_t size() const { return heap.size(); }

	  private:
		struct Slot {
			Vertex* key;
			int		handle;
		};

		int	 allocHandle();
		void siftUp(int i);
		void siftDown(int i);
		void swapSlots(int i, int j);

		std::vector<Slot> heap;			// 0-indexed binary heap
		std::vector<int>  handleToPos;	// handle -> heap index, or kInvalidHandle
		std::vector<int>  freeHandles;	// recycled handle ids
	};

} // namespace renderer::tess
