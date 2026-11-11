#include "RunPlanner.h"

#include <algorithm>
#include <bit>

namespace worldview {

	void RunPlanner::beginFrame(int viewportWidth, int viewportHeight, uint32_t meshCount) {
		all.beginFrame(viewportWidth, viewportHeight);
		large.beginFrame(viewportWidth, viewportHeight);
		for (const uint32_t mesh : meshOfOrder) {
			orderOfMesh[mesh] = kNone;
		}
		for (size_t order = 0; order < meshOfOrder.size(); ++order) {
			drawOfLayer[order].clear();
		}
		meshOfOrder.clear();
		meshBound = std::bit_ceil(std::max(meshCount, 1U));
		meshShift = static_cast<uint32_t>(std::countr_zero(meshBound));
		if (orderOfMesh.size() < meshBound) {
			orderOfMesh.resize(meshBound, kNone);
		}
		draws.clear();
		firstOpen = 0;
		floorLayer = 0;
		topLayer = 0;
		closes = 0;
		splitCount = 0;
	}

	uint32_t RunPlanner::place(uint32_t mesh, const OverlapGrid::Cells& cells, bool small) {
		uint32_t order = orderOfMesh[mesh];
		if (order == kNone) {
			order = static_cast<uint32_t>(meshOfOrder.size());
			orderOfMesh[mesh] = order;
			meshOfOrder.push_back(mesh);
			if (drawOfLayer.size() <= order) {
				drawOfLayer.resize(static_cast<size_t>(order) + 1);
				openedAt.resize(static_cast<size_t>(order) + 1);
			}
			openedAt[order] = kNone;
		}

		uint32_t	   layer = floorLayer;
		const uint32_t latest = (small ? large : all).latest(cells);
		if (latest >= keyOf(floorLayer, 0)) {
			const uint32_t latestLayer = (latest - 1) >> meshShift;
			const uint32_t latestOrder = (latest - 1) & (meshBound - 1);
			layer = latestOrder <= order ? latestLayer : latestLayer + 1;
		}

		std::vector<uint32_t>& byLayer = drawOfLayer[order];
		if (byLayer.size() <= layer) {
			byLayer.resize(static_cast<size_t>(layer) + 1, kNone);
		}
		if (byLayer[layer] == kNone) {
			// A mesh that already has an open draw opens another only because of an overlap.
			splitCount += openedAt[order] == closes ? 1 : 0;
			openedAt[order] = closes;
			byLayer[layer] = static_cast<uint32_t>(draws.size());
			draws.push_back({.key = keyOf(layer, order)});
			topLayer = std::max(topLayer, layer);
		}

		const uint32_t key = keyOf(layer, order);
		all.mark(cells, key);
		if (!small) {
			large.mark(cells, key);
		}
		return byLayer[layer];
	}

	std::span<const uint32_t> RunPlanner::close() {
		closing.clear();
		for (auto draw = static_cast<uint32_t>(firstOpen); draw < draws.size(); ++draw) {
			closing.push_back(draw);
		}
		std::sort(closing.begin(), closing.end(), [this](uint32_t a, uint32_t b) { return draws[a].key < draws[b].key; });
		if (!closing.empty()) {
			floorLayer = topLayer + 1;
			topLayer = floorLayer;
		}
		firstOpen = static_cast<uint32_t>(draws.size());
		++closes;
		return closing;
	}

} // namespace worldview
