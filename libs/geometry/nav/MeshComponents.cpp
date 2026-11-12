#include "MeshComponents.h"

#include "PatchableMesh.h"

#include <algorithm>
#include <cassert>
#include <numeric>
#include <tuple>

// The update, from a patch's report:
//   1. Freed triangles leave their labels; a label that lost any is "lost".
//   2. The new passable triangles are flooded into groups. A group attaches to every kept
//      passable triangle beside it; a kept triangle two groups touch joins them. A kept
//      border triangle of a lost label that no group touches is a group of its own.
//   3. A label's kept ground can meet the groups at several places. If the label lost
//      nothing, its ground is still connected, so every group it touches is joined. If it
//      lost triangles, each distinct group class floods the label's kept ground in
//      lockstep, one triangle a turn; floods that meet join their classes, and a class
//      whose flood runs out is a piece cut off. When one class is left running, the
//      rest of the label is its, unvisited.
//   4. Each final class takes the label of its largest unvisited rest and relabels what
//      else it holds (smaller rests by flood, cut-off pieces from their lists, new
//      triangles); a class with no rest takes a free label.
// Work is the patch's own triangles, plus the lockstep floods (about the number of
// meeting places times the smaller sides) and the relabelled smaller components.

namespace geometry::nav {

	MeshComponents::MeshComponents(const MeshComponentsArgs& args) : openTags(args.openTags) {}

	bool MeshComponents::passable(const NavTriangle& t) const {
		return !isCommonKnowledgeTerrainFace(t) || std::find(openTags.begin(), openTags.end(), t.faceBlocker) != openTags.end();
	}

	bool MeshComponents::passableAt(const NavView& view, std::int32_t t) const {
		return view.isLive(t) && passable(view.triangle(t));
	}

	ComponentLabel MeshComponents::label(std::int32_t t) const {
		return t >= 0 && static_cast<std::size_t>(t) < labels.size() ? labels[static_cast<std::size_t>(t)] : kNoComponent;
	}

	bool MeshComponents::sameComponent(std::int32_t a, std::int32_t b) const {
		const ComponentLabel la = label(a);
		return la != kNoComponent && la == label(b);
	}

	std::int32_t MeshComponents::markOf(std::int32_t t) const {
		const std::uint64_t m = marks[static_cast<std::size_t>(t)];
		return static_cast<std::uint32_t>(m >> 32) == markStamp ? static_cast<std::int32_t>(static_cast<std::uint32_t>(m)) : -1;
	}

	void MeshComponents::setMark(std::int32_t t, std::int32_t value) {
		marks[static_cast<std::size_t>(t)] = (static_cast<std::uint64_t>(markStamp) << 32) | static_cast<std::uint32_t>(value);
	}

	std::int32_t MeshComponents::findGroup(std::int32_t g) {
		while (groupParent[static_cast<std::size_t>(g)] != g) {
			const std::int32_t up = groupParent[static_cast<std::size_t>(groupParent[static_cast<std::size_t>(g)])];
			groupParent[static_cast<std::size_t>(g)] = up;
			g										 = up;
		}
		return g;
	}

	void MeshComponents::uniteGroups(std::int32_t a, std::int32_t b) {
		a = findGroup(a);
		b = findGroup(b);
		if (a != b) {
			groupParent[static_cast<std::size_t>(std::max(a, b))] = std::min(a, b);
		}
	}

	ComponentLabel MeshComponents::takeLabel() {
		if (!freeLabels.empty()) {
			const ComponentLabel l = freeLabels.back();
			freeLabels.pop_back();
			return l;
		}
		sizes.push_back(0);
		lost.push_back(0);
		return static_cast<ComponentLabel>(sizes.size() - 1);
	}

	void MeshComponents::dropTriangles(ComponentLabel l, std::int32_t count) {
		sizes[static_cast<std::size_t>(l)] -= count;
		if (lost[static_cast<std::size_t>(l)] == 0) {
			lost[static_cast<std::size_t>(l)] = 1;
			lostLabels.push_back(l);
		}
	}

	// Floods the rest from its meeting triangles in this class. Listed pieces of the same
	// label in the class are relabelled first, so the flood never enters them.
	std::int32_t MeshComponents::relabelRest(const NavView& view, const Piece& rest, std::int32_t root, ComponentLabel to) {
		const ComponentLabel from = rest.label;
		flood.clear();
		for (std::int32_t k = rest.begin; k < rest.end; ++k) {
			const Attachment& a = attachments[static_cast<std::size_t>(k)];
			if (labels[static_cast<std::size_t>(a.tri)] == from && findGroup(a.group) == root) {
				labels[static_cast<std::size_t>(a.tri)] = to;
				flood.push_back(a.tri);
			}
		}
		for (std::size_t head = 0; head < flood.size(); ++head) {
			for (const std::int32_t nb : view.triangle(flood[head]).neighbor) {
				if (nb >= 0 && labels[static_cast<std::size_t>(nb)] == from) {
					labels[static_cast<std::size_t>(nb)] = to;
					flood.push_back(nb);
				}
			}
		}
		return static_cast<std::int32_t>(flood.size());
	}

	void MeshComponents::checkSplit(const NavView& view, ComponentLabel l, std::size_t begin, std::size_t end) {
		if (++markStamp == 0) {
			marks.zero();
			markStamp = 1;
		}
		// One side per group class the label meets, seeded with its meeting triangles.
		std::size_t sideCount = 0;
		for (std::size_t k = begin; k < end; ++k) {
			const Attachment&  a	= attachments[k];
			const std::int32_t root = findGroup(a.group);
			std::size_t		   s	= 0;
			while (s < sideCount && sides[s].group != root) {
				++s;
			}
			if (s == sideCount) {
				if (sides.size() == sideCount) {
					sides.emplace_back();
				}
				sides[s].group = root;
				sides[s].queue.clear();
				sides[s].head = 0;
				++sideCount;
			}
			if (markOf(a.tri) < 0) {
				setMark(a.tri, static_cast<std::int32_t>(s));
				sides[s].queue.push_back(a.tri);
			}
			assert(!sides[s].queue.empty() && "MeshComponents: a kept triangle meets two group classes");
		}
		const std::size_t firstPiece = pieces.size();
		auto			  addRest	 = [&](std::int32_t group) {
			std::int32_t listed = 0;
			for (std::size_t p = firstPiece; p < pieces.size(); ++p) {
				listed += pieces[p].size;
			}
			pieces.push_back({l, group, static_cast<std::int32_t>(begin), static_cast<std::int32_t>(end), sizes[static_cast<std::size_t>(l)] - listed, true, false});
		};
		if (sideCount == 1) {
			addRest(sides[0].group);
			return;
		}

		// Sides that meet are joined in a union-find over sides; a joined side's open count
		// is how many of its members still have triangles to visit.
		sideParent.resize(sideCount);
		std::iota(sideParent.begin(), sideParent.end(), 0);
		auto findSide = [&](std::int32_t s) {
			while (sideParent[static_cast<std::size_t>(s)] != s) {
				s = sideParent[static_cast<std::size_t>(s)];
			}
			return s;
		};
		openMembers.assign(sideCount, 1);
		std::size_t				  openSides = sideCount;
		auto					  listPiece = [&](std::int32_t root) {
			 const std::int32_t first = static_cast<std::int32_t>(pieceTris.size());
			 for (std::size_t s = 0; s < sideCount; ++s) {
				 if (findSide(static_cast<std::int32_t>(s)) == root) {
					 pieceTris.insert(pieceTris.end(), sides[s].queue.begin(), sides[s].queue.end());
				 }
			 }
			 const std::int32_t last = static_cast<std::int32_t>(pieceTris.size());
			 pieces.push_back({l, sides[static_cast<std::size_t>(root)].group, first, last, last - first, false, true});
		};
		while (openSides > 1) {
			for (std::size_t s = 0; s < sideCount && openSides > 1; ++s) {
				Side& side = sides[s];
				if (side.head >= side.queue.size()) {
					continue;
				}
				const std::int32_t t = side.queue[side.head++];
				++upkeep.visited;
				for (const std::int32_t nb : view.triangle(t).neighbor) {
					if (nb < 0 || labels[static_cast<std::size_t>(nb)] != l) {
						continue;
					}
					const std::int32_t m = markOf(nb);
					if (m < 0) {
						setMark(nb, static_cast<std::int32_t>(s));
						side.queue.push_back(nb);
						continue;
					}
					const std::int32_t a = findSide(m);
					const std::int32_t b = findSide(static_cast<std::int32_t>(s));
					if (a != b) {
						assert(openMembers[static_cast<std::size_t>(a)] > 0 && openMembers[static_cast<std::size_t>(b)] > 0);
						const std::int32_t root = std::min(a, b);
						const std::int32_t gone = std::max(a, b);
						sideParent[static_cast<std::size_t>(gone)] = root;
						openMembers[static_cast<std::size_t>(root)] += openMembers[static_cast<std::size_t>(gone)];
						--openSides;
						uniteGroups(sides[static_cast<std::size_t>(a)].group, sides[static_cast<std::size_t>(b)].group);
					}
				}
				if (side.head == side.queue.size()) {
					const std::int32_t root = findSide(static_cast<std::int32_t>(s));
					if (--openMembers[static_cast<std::size_t>(root)] == 0) {
						--openSides;
						listPiece(root);
					}
				}
			}
		}
		if (openSides == 1) {
			for (std::size_t s = 0; s < sideCount; ++s) {
				const std::int32_t root = findSide(static_cast<std::int32_t>(s));
				if (openMembers[static_cast<std::size_t>(root)] > 0) {
					addRest(sides[static_cast<std::size_t>(root)].group);
					return;
				}
			}
		}
		// Every flood ran out: the largest listed piece is the rest.
		std::size_t largest = firstPiece;
		for (std::size_t p = firstPiece + 1; p < pieces.size(); ++p) {
			if (pieces[p].size > pieces[largest].size) {
				largest = p;
			}
		}
		pieces[largest].rest = true;
	}

	void MeshComponents::update(const NavView& view, const PatchReport& report) {
		upkeep					= {};
		const std::size_t slots = static_cast<std::size_t>(view.triangleSlots());
		while (labels.size() < slots) {
			labels.push_back(kNoComponent);
		}
		marks.growTo(slots);
		lost.resize(sizes.size(), 0);
		lostLabels.clear();

		for (const std::int32_t f : report.freedTriangles) {
			const ComponentLabel l = labels[static_cast<std::size_t>(f)];
			if (l != kNoComponent) {
				labels[static_cast<std::size_t>(f)] = kNoComponent;
				dropTriangles(l, 1);
			}
		}

		if (++markStamp == 0) {
			marks.zero();
			markStamp = 1;
		}
		groupTris.clear();
		groupBegin.clear();
		groupParent.clear();
		attachments.clear();
		const std::vector<std::int32_t>& created   = report.newTriangles;
		auto							 isCreated = [&](std::int32_t t) { return std::binary_search(created.begin(), created.end(), t); };
		auto							 newGroup  = [&] {
			  const std::int32_t g = static_cast<std::int32_t>(groupBegin.size());
			  groupBegin.push_back(static_cast<std::int32_t>(groupTris.size()));
			  groupParent.push_back(g);
			  return g;
		};
		for (const std::int32_t t : created) {
			if (!passableAt(view, t) || markOf(t) >= 0) {
				continue;
			}
			const std::int32_t g = newGroup();
			setMark(t, g);
			groupTris.push_back(t);
			for (std::size_t i = static_cast<std::size_t>(groupBegin[static_cast<std::size_t>(g)]); i < groupTris.size(); ++i) {
				for (const std::int32_t nb : view.triangle(groupTris[i]).neighbor) {
					if (nb < 0 || !passableAt(view, nb)) {
						continue;
					}
					const std::int32_t m = markOf(nb);
					if (m == g) {
						continue;
					}
					if (isCreated(nb)) {
						setMark(nb, g);
						groupTris.push_back(nb);
						continue;
					}
					if (m >= 0) {
						uniteGroups(m, g);
					}
					setMark(nb, g);
					attachments.push_back({labels[static_cast<std::size_t>(nb)], g, nb});
				}
			}
		}
		const std::int32_t groupsWithTris = static_cast<std::int32_t>(groupBegin.size());
		for (const ChangedTriangle& c : report.changes) {
			if ((c.why & kTriangleBorder) == 0 || (c.why & kTriangleCreated) != 0 || !passableAt(view, c.id) || markOf(c.id) >= 0) {
				continue;
			}
			const ComponentLabel l = labels[static_cast<std::size_t>(c.id)];
			if (lost[static_cast<std::size_t>(l)] == 0) {
				continue;
			}
			const std::int32_t g = newGroup();
			setMark(c.id, g);
			attachments.push_back({l, g, c.id});
		}
		groupBegin.push_back(static_cast<std::int32_t>(groupTris.size()));

		std::sort(attachments.begin(), attachments.end(),
				  [](const Attachment& a, const Attachment& b) { return std::tie(a.label, a.group, a.tri) < std::tie(b.label, b.group, b.tri); });
		pieces.clear();
		pieceTris.clear();
		for (std::size_t i = 0; i < attachments.size();) {
			const ComponentLabel l = attachments[i].label;
			assert(l != kNoComponent && "MeshComponents: kept passable ground without a label");
			std::size_t j = i + 1;
			while (j < attachments.size() && attachments[j].label == l) {
				++j;
			}
			if (lost[static_cast<std::size_t>(l)] == 0) {
				for (std::size_t k = i + 1; k < j; ++k) {
					uniteGroups(attachments[i].group, attachments[k].group);
				}
				pieces.push_back({l, attachments[i].group, static_cast<std::int32_t>(i), static_cast<std::int32_t>(j), sizes[static_cast<std::size_t>(l)], true, false});
			} else {
				checkSplit(view, l, i, j);
			}
			i = j;
		}

		// Each class of joined groups, with the pieces and new triangles it holds.
		byRoot.clear();
		for (std::size_t p = 0; p < pieces.size(); ++p) {
			byRoot.push_back({findGroup(pieces[p].group), static_cast<std::int32_t>(p)});
		}
		for (std::int32_t g = 0; g < groupsWithTris; ++g) {
			byRoot.push_back({findGroup(g), -1 - g});
		}
		std::sort(byRoot.begin(), byRoot.end());
		for (std::size_t i = 0; i < byRoot.size();) {
			std::size_t j = i;
			while (j < byRoot.size() && byRoot[j].first == byRoot[i].first) {
				++j;
			}
			std::int32_t keeper = -1;
			for (std::size_t k = i; k < j; ++k) {
				if (byRoot[k].second < 0) {
					continue;
				}
				const Piece& p = pieces[static_cast<std::size_t>(byRoot[k].second)];
				if (p.rest && (keeper < 0 || p.size > pieces[static_cast<std::size_t>(keeper)].size ||
							   (p.size == pieces[static_cast<std::size_t>(keeper)].size && p.label < pieces[static_cast<std::size_t>(keeper)].label))) {
					keeper = byRoot[k].second;
				}
			}
			ComponentLabel to = kNoComponent;
			if (keeper >= 0) {
				to = pieces[static_cast<std::size_t>(keeper)].label;
			} else {
				to = takeLabel();
				++upkeep.fresh;
			}
			// New triangles and listed pieces first, then the rests (see relabelRest). A
			// piece already under the class's label rejoined it through new ground.
			for (const bool rests : {false, true}) {
				for (std::size_t k = i; k < j; ++k) {
					const std::int32_t entry = byRoot[k].second;
					if (entry < 0) {
						if (!rests) {
							const std::int32_t g = -1 - entry;
							for (std::int32_t at = groupBegin[static_cast<std::size_t>(g)]; at < groupBegin[static_cast<std::size_t>(g) + 1]; ++at) {
								labels[static_cast<std::size_t>(groupTris[static_cast<std::size_t>(at)])] = to;
							}
							sizes[static_cast<std::size_t>(to)] += groupBegin[static_cast<std::size_t>(g) + 1] - groupBegin[static_cast<std::size_t>(g)];
						}
						continue;
					}
					const Piece& p = pieces[static_cast<std::size_t>(entry)];
					if (p.label == to || p.listed == rests) {
						continue;
					}
					std::int32_t moved = 0;
					if (p.listed) {
						for (std::int32_t at = p.begin; at < p.end; ++at) {
							labels[static_cast<std::size_t>(pieceTris[static_cast<std::size_t>(at)])] = to;
						}
						moved = p.end - p.begin;
					} else {
						moved = relabelRest(view, p, byRoot[i].first, to);
						upkeep.visited += moved;
					}
					if (p.rest) {
						++upkeep.merges;
					} else {
						++upkeep.splits;
					}
					sizes[static_cast<std::size_t>(to)] += moved;
					dropTriangles(p.label, moved);
					upkeep.relabelled += moved;
				}
			}
			i = j;
		}

		for (const ComponentLabel l : lostLabels) {
			lost[static_cast<std::size_t>(l)] = 0;
			if (sizes[static_cast<std::size_t>(l)] == 0) {
				freeLabels.push_back(l);
				++upkeep.emptied;
			}
		}
		upkeep.emptied -= upkeep.merges;
		// A split whose whole component was re-derived shows as fresh labels, not a split.
		if (report.opens || upkeep.splits > 0 || upkeep.merges > 0 || upkeep.fresh > 0) {
			connectivityEpoch = nextMeshGeneration();
		}
	}

} // namespace geometry::nav
