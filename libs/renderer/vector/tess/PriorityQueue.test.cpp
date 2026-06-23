#include "PriorityQueue.h"

#include <gtest/gtest.h>

#include <vector>

namespace renderer::tess {
	namespace {
		Vertex makeV(float s, float t) {
			Vertex v{};
			v.s = s;
			v.t = t;
			return v;
		}
	} // namespace

	TEST(TessPriorityQueue, ExtractsInLexOrder) {
		std::vector<Vertex> verts = {makeV(3, 1), makeV(1, 5), makeV(1, 2),
									 makeV(2, 0), makeV(3, 0), makeV(0, 9)};
		PriorityQueue		pq;
		for (auto& v : verts) {
			pq.insert(&v);
		}
		ASSERT_EQ(pq.size(), verts.size());

		Vertex* prev = pq.extractMin();
		ASSERT_NE(prev, nullptr);
		int count = 1;
		while (!pq.empty()) {
			Vertex* cur = pq.extractMin();
			ASSERT_NE(cur, nullptr);
			EXPECT_TRUE(prev->s < cur->s || (prev->s == cur->s && prev->t <= cur->t));
			prev = cur;
			++count;
		}
		EXPECT_EQ(count, static_cast<int>(verts.size()));
		EXPECT_EQ(pq.extractMin(), nullptr);
	}

	TEST(TessPriorityQueue, MinimumPeeks) {
		std::vector<Vertex> verts = {makeV(5, 5), makeV(1, 1), makeV(3, 3)};
		PriorityQueue		pq;
		for (auto& v : verts) {
			pq.insert(&v);
		}
		EXPECT_EQ(pq.minimum(), &verts[1]); // (1,1) is smallest
		pq.extractMin();
		EXPECT_EQ(pq.minimum(), &verts[2]); // (3,3) is next
	}

	TEST(TessPriorityQueue, RemoveByHandleSkipsVertex) {
		std::vector<Vertex> verts = {makeV(1, 0), makeV(2, 0), makeV(3, 0), makeV(4, 0)};
		PriorityQueue		pq;
		std::vector<int>	handles;
		for (auto& v : verts) {
			handles.push_back(pq.insert(&v));
		}
		pq.remove(handles[1]); // drop (2,0)
		EXPECT_EQ(pq.extractMin(), &verts[0]);
		EXPECT_EQ(pq.extractMin(), &verts[2]);
		EXPECT_EQ(pq.extractMin(), &verts[3]);
		EXPECT_TRUE(pq.empty());
	}

	TEST(TessPriorityQueue, RemoveIsIdempotentAndHandlesRecycle) {
		std::vector<Vertex> verts = {makeV(1, 0), makeV(2, 0)};
		PriorityQueue		pq;
		const int			h0 = pq.insert(&verts[0]);
		pq.insert(&verts[1]);
		pq.remove(h0);
		pq.remove(h0); // double remove is a no-op

		Vertex extra = makeV(0, 0);
		pq.insert(&extra); // reuses the freed handle slot
		EXPECT_EQ(pq.extractMin(), &extra);
		EXPECT_EQ(pq.extractMin(), &verts[1]);
		EXPECT_TRUE(pq.empty());
	}

} // namespace renderer::tess
