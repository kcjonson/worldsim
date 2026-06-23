#include "PriorityQueue.h"

namespace renderer::tess {

	namespace {
		// Strict lexicographic order. A min-heap on this returns the same vertex order
		// as libtess's tesvertLeq-based queue; equal-coordinate vertices stay adjacent.
		bool less(const Vertex* a, const Vertex* b) {
			return (a->s < b->s) || (a->s == b->s && a->t < b->t);
		}
	} // namespace

	int PriorityQueue::allocHandle() {
		if (!freeHandles.empty()) {
			const int h = freeHandles.back();
			freeHandles.pop_back();
			return h;
		}
		handleToPos.push_back(kInvalidHandle);
		return static_cast<int>(handleToPos.size()) - 1;
	}

	void PriorityQueue::swapSlots(int i, int j) {
		std::swap(heap[i], heap[j]);
		handleToPos[heap[i].handle] = i;
		handleToPos[heap[j].handle] = j;
	}

	void PriorityQueue::siftUp(int i) {
		while (i > 0) {
			const int parent = (i - 1) / 2;
			if (!less(heap[i].key, heap[parent].key)) {
				break;
			}
			swapSlots(i, parent);
			i = parent;
		}
	}

	void PriorityQueue::siftDown(int i) {
		const int n = static_cast<int>(heap.size());
		for (;;) {
			const int left = (2 * i) + 1;
			const int right = (2 * i) + 2;
			int		  smallest = i;
			if (left < n && less(heap[left].key, heap[smallest].key)) {
				smallest = left;
			}
			if (right < n && less(heap[right].key, heap[smallest].key)) {
				smallest = right;
			}
			if (smallest == i) {
				break;
			}
			swapSlots(i, smallest);
			i = smallest;
		}
	}

	int PriorityQueue::insert(Vertex* v) {
		const int handle = allocHandle();
		const int pos = static_cast<int>(heap.size());
		heap.push_back({v, handle});
		handleToPos[handle] = pos;
		siftUp(pos);
		return handle;
	}

	Vertex* PriorityQueue::minimum() const {
		return heap.empty() ? nullptr : heap.front().key;
	}

	Vertex* PriorityQueue::extractMin() {
		if (heap.empty()) {
			return nullptr;
		}
		const Slot minSlot = heap.front();
		handleToPos[minSlot.handle] = kInvalidHandle;
		freeHandles.push_back(minSlot.handle);

		if (heap.size() == 1) {
			heap.pop_back();
			return minSlot.key;
		}
		heap.front() = heap.back();
		handleToPos[heap.front().handle] = 0;
		heap.pop_back();
		siftDown(0);
		return minSlot.key;
	}

	void PriorityQueue::remove(int handle) {
		if (handle < 0 || handle >= static_cast<int>(handleToPos.size())) {
			return;
		}
		const int pos = handleToPos[handle];
		if (pos == kInvalidHandle) {
			return;
		}
		handleToPos[handle] = kInvalidHandle;
		freeHandles.push_back(handle);

		const int last = static_cast<int>(heap.size()) - 1;
		if (pos == last) {
			heap.pop_back();
			return;
		}
		heap[pos] = heap.back();
		handleToPos[heap[pos].handle] = pos;
		heap.pop_back();
		// The moved element may belong higher or lower than its new slot.
		siftUp(pos);
		siftDown(pos);
	}

} // namespace renderer::tess
