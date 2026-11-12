#pragma once

#include "../core/PagedVector.h"
#include "../core/ScratchArray.h"
#include "NavMesh.h"

#include <cstdint>
#include <vector>

// Connected components of a PatchableMesh's ground, one label per triangle, kept up to
// date by every patch. Two triangles share a label when a disc of zero width can walk
// from one to the other over the mesh with every wall read open, so a different label
// means no mover of any size or belief can cross between them on meshed ground.
//
// What a label reads as open is decided by face tag: floor, any wall face (tag > 0), and
// the negative tags named in MeshComponentsArgs::openTags. Every other negative tag
// (water, placed obstacles, wall junctions) blocks. A tag class that must never cut a
// component, such as mover footprints, goes in openTags.

namespace geometry::nav {

	struct PatchReport;

	using ComponentLabel					= std::int32_t;
	constexpr ComponentLabel kNoComponent	= -1;

	struct MeshComponentsArgs {
		std::vector<std::int64_t> openTags;
	};

	// What one update did, counted.
	struct ComponentUpkeep {
		std::int32_t splits		= 0; // pieces cut off a component and given a label of their own
		std::int32_t merges		= 0; // components joined into another
		std::int32_t fresh		= 0; // labels taken for ground joined to no labelled ground
		std::int32_t emptied	= 0; // components whose last triangle went
		std::int64_t visited	= 0; // triangles read by the split check and relabelling
		std::int64_t relabelled = 0; // kept triangles whose label changed
	};

	class MeshComponents {
	  public:
		explicit MeshComponents(const MeshComponentsArgs& args = {});

		bool passable(const NavTriangle& t) const;

		// Brings the labels up to date with a patch: the report's freed, new, and border
		// triangles, read from the mesh the view shows after it.
		void update(const NavView& view, const PatchReport& report);

		ComponentLabel label(std::int32_t t) const;
		// False when either id is off the mesh or on ground that blocks.
		bool		   sameComponent(std::int32_t a, std::int32_t b) const;
		std::int32_t   componentSize(ComponentLabel label) const { return sizes[static_cast<std::size_t>(label)]; }

		// Moves when labels join, split, or are taken for new ground, and on any patch that
		// may open a way for some mover and belief inside a label (PatchReport::opens). A
		// patch that only closes ground and splits nothing leaves it, unless it re-derived a
		// whole component (that takes a fresh label or empties one). Values never repeat
		// (nextMeshGeneration).
		std::uint64_t epoch() const { return connectivityEpoch; }
		// A way may have opened for some belief with no patch (a door added): the epoch
		// moves, the labels stay.
		void noteOpening() { connectivityEpoch = nextMeshGeneration(); }

		const ComponentUpkeep& lastUpkeep() const { return upkeep; }

	  private:
		struct Attachment {
			ComponentLabel label = kNoComponent;
			std::int32_t   group = -1;
			std::int32_t   tri	 = -1;
		};
		// A label's kept ground that ends up in one group class: either listed (a piece the
		// split check flooded to its end) or the rest of the label, never enumerated. A rest
		// can be several parts of kept ground joined only through new triangles; every
		// part touches the patch, so its meeting triangles reach them all.
		struct Piece {
			ComponentLabel label  = kNoComponent;
			std::int32_t   group  = -1;
			std::int32_t   begin  = 0; // listed: into pieceTris; else into attachments
			std::int32_t   end	  = 0;
			std::int32_t   size	  = 0;
			bool		   rest	  = false;
			bool		   listed = false;
		};
		struct Side {
			std::int32_t			  group = -1;
			std::vector<std::int32_t> queue; // every triangle it visited, in order
			std::size_t				  head	= 0;
		};

		bool		   passableAt(const NavView& view, std::int32_t t) const;
		std::int32_t   markOf(std::int32_t t) const;
		void		   setMark(std::int32_t t, std::int32_t value);
		std::int32_t   findGroup(std::int32_t g);
		void		   uniteGroups(std::int32_t a, std::int32_t b);
		ComponentLabel takeLabel();
		void		   dropTriangles(ComponentLabel label, std::int32_t count);
		void		   checkSplit(const NavView& view, ComponentLabel label, std::size_t begin, std::size_t end);
		std::int32_t   relabelRest(const NavView& view, const Piece& rest, std::int32_t root, ComponentLabel to);

		std::vector<std::int64_t> openTags;
		PagedVector<ComponentLabel> labels; // per triangle slot
		std::vector<std::int32_t>	sizes;	// triangles per label, 0 when free
		std::vector<ComponentLabel> freeLabels;
		std::uint64_t				connectivityEpoch = nextMeshGeneration();
		ComponentUpkeep				upkeep;

		// update scratch
		ScratchArray<std::uint64_t> marks; // per triangle: stamp << 32 | value
		std::uint32_t				markStamp = 0;
		std::vector<std::uint8_t>	lost;	   // per label: lost triangles this update
		std::vector<ComponentLabel> lostLabels;
		std::vector<std::int32_t>	groupTris; // new triangles, by group
		std::vector<std::int32_t>	groupBegin;
		std::vector<std::int32_t>	groupParent;
		std::vector<Attachment>		attachments;
		std::vector<Piece>			pieces;
		std::vector<std::int32_t>	pieceTris;
		std::vector<Side>			sides;
		std::vector<std::int32_t>	flood;
		std::vector<std::int32_t>	sideParent;
		std::vector<std::int32_t>	openMembers;
		std::vector<std::pair<std::int32_t, std::int32_t>> byRoot;
	};

} // namespace geometry::nav
