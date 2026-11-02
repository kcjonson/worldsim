#pragma once

// The generators' patch noise: gradient fBm (salal HashNoise) over a chunk's tiles. Hashed value
// noise, which this replaced, draws straight axis-aligned runs wherever two corners of a lattice cell
// hash alike; gradient noise has no such bias. Each octave's lattice corners are hashed once for the
// chunk (NoiseOctaveGrid), since a gradient octave read cold costs four hashes a tile.

#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/NoiseLattice.h"

#include <array>
#include <cstdint>
#include <optional>

namespace world::generation {

	/// The fields the generators read, one seed each.
	enum class PatchField : uint8_t {
		Moisture,	  ///< grass variants (grassland, forest)
		Outcrop,	  ///< rock outcrops, forest dirt and mountain snow
		Dirt,		  ///< grassland's exposed soil
		Fertility,	  ///< grassland meadows
		MarshIslands, ///< wetland grass
		Count
	};

	/// The patch fields over one chunk's tiles, each built on first use. A value depends only on the
	/// world tile and seed, never on the chunk that reads it. One per chunk per generation call: not
	/// thread-safe.
	class PatchNoise {
	  public:
		PatchNoise(ChunkCoordinate coord, uint64_t worldSeed);

		/// `field` at the center of local tile (localX, localY): two octaves, the second half the
		/// wavelength and half as strong. Within [-1, 1], centered on 0; a threshold is a share of
		/// tiles, not a fraction of the range (each generator notes its shares).
		[[nodiscard]] float at(PatchField field, uint16_t localX, uint16_t localY) const {
			std::optional<Octaves>& octaves = fields[static_cast<size_t>(field)];
			if (!octaves) {
				octaves.emplace(build(field));
			}
			return (octaves->first.at(localX, localY) + 0.5F * octaves->second.at(localX, localY)) / 1.5F;
		}

	  private:
		struct Octaves {
			NoiseOctaveGrid first;
			NoiseOctaveGrid second;
		};

		[[nodiscard]] Octaves build(PatchField field) const;

		ChunkCoordinate																	   coord;
		uint64_t																		   worldSeed;
		mutable std::array<std::optional<Octaves>, static_cast<size_t>(PatchField::Count)> fields;
	};

} // namespace world::generation
