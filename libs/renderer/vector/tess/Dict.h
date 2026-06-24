#pragma once

// Sorted doubly-linked dictionary of active regions, ordered by a sweep-state
// dependent comparator. Insert and search are linear scans, which is fine at the
// shape sizes we tessellate. Ported from libtess2 (dict.c). The comparator takes
// an opaque frame pointer (the sweep) so the dictionary stays independent of it.

#include "Mesh.h" // ActiveRegion (forward-declared)

#include <deque>

namespace renderer::tess {

	struct DictNode {
		ActiveRegion* key{nullptr};
		DictNode*	  next{nullptr};
		DictNode*	  prev{nullptr};
	};

	class Dict {
	  public:
		using LeqFn = bool (*)(void* frame, ActiveRegion* a, ActiveRegion* b);

		Dict(void* dictFrame, LeqFn dictLeq);

		Dict(const Dict&) = delete;
		Dict& operator=(const Dict&) = delete;
		Dict(Dict&&) = delete;
		Dict& operator=(Dict&&) = delete;
		~Dict() = default;

		DictNode* insert(ActiveRegion* key) { return insertBefore(&head, key); }
		DictNode* insertBefore(DictNode* node, ActiveRegion* key);
		void	  remove(DictNode* node);
		// Smallest node whose key is >= the given key (key == null sentinel if none).
		DictNode* search(ActiveRegion* key);

		DictNode* min() { return head.next; }
		DictNode* max() { return head.prev; }

	  private:
		DictNode			 head{};
		void*				 frame{nullptr};
		LeqFn				 leq{nullptr};
		std::deque<DictNode> pool;
	};

	inline ActiveRegion* dictKey(DictNode* n) { return n->key; }
	inline DictNode*	 dictSucc(DictNode* n) { return n->next; }
	inline DictNode*	 dictPred(DictNode* n) { return n->prev; }

} // namespace renderer::tess
