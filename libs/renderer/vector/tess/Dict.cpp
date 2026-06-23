#include "Dict.h"

namespace renderer::tess {

	Dict::Dict(void* dictFrame, LeqFn dictLeq) : frame(dictFrame), leq(dictLeq) {
		head.key = nullptr;
		head.next = &head;
		head.prev = &head;
	}

	DictNode* Dict::insertBefore(DictNode* node, ActiveRegion* key) {
		do {
			node = node->prev;
		} while (node->key != nullptr && !leq(frame, node->key, key));

		DictNode& newNode = pool.emplace_back();
		newNode.key = key;
		newNode.next = node->next;
		node->next->prev = &newNode;
		newNode.prev = node;
		node->next = &newNode;
		return &newNode;
	}

	void Dict::remove(DictNode* node) {
		node->next->prev = node->prev;
		node->prev->next = node->next;
		// Storage is not reclaimed; the pool frees it with the dictionary.
	}

	DictNode* Dict::search(ActiveRegion* key) {
		DictNode* node = &head;
		do {
			node = node->next;
		} while (node->key != nullptr && !leq(frame, key, node->key));
		return node;
	}

} // namespace renderer::tess
