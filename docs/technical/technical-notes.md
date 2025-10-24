# Technical Notes and Research

This document contains research notes, open questions, and technical considerations for various aspects of the game. These are working notes and may be superceded by more formal technical design documents as systems are implemented.

## Jargon / Terminology

- **Tessellating a sphere** - Dividing a sphere's surface into tiles
- **Icosahedron** - 20-sided polyhedron with equilateral triangular faces
- **Icosphere** - A sphere generated from a subdivided Icosahedron
- **Goldberg polyhedra** - A class of convex polyhedra made from hexagons and pentagons

## Open Questions

### World Generation and Rendering

**Complex planet simulation:**
- How will we separate generating a VERY complex planet from actually rendering it? If I were to calculate and simulate a planet with a MILLION tiles, we obviously couldn't render that.
- How does the translation from a 3D data structure to 2D work? Sample across the surface?
- How will we define chunks and boundaries when sampling from 3D so that the edges align, but still have a high level of detail?

### Project Structure

**Organization:**
- How should the project be divided up?
- How should unit tests be handled? Should they be built? Have their own makefile?
- Is dependency management via VCPKG cool?
- What would the first module/subproject to pull out be? World Gen? Game? How does this affect the build?
- The "worldgen" code could just be creating the data structure and be totally separate from the "worldrenderer"
- What data structure should be used for the world data?

### OpenGL Implementation

**Caching:**
- After a render is done, can the pixels just be cached and re-applied? For instance if the globe is rendered once, how do we not re-render it until something actually changes?
- If we know a render is going to take a long time, how do we render to a place then just take the pixels and show them?
- There seems to be a lot to a reset and redraw… too much?

**Camera and rendering:**
- Zooming: Should we be moving the camera on the 2d map?
- Where the hell do shaders go?
- Explain vertex and fragment to me
- Why are there so many shader files? How complex should these be?

## World Generation Process

### High-Level Steps

1. **Create spherical tileset** based on a subdivided Icosahedron. This tileset should have hundreds of thousands, if not millions of tiles. There needs to be enough resolution so that rivers can form and not seem out of scale when on the planet. If each pentagon was 10m wide, it would take 2.9 TRILLION of them to cover the earth. Just for reference…

2. **Generate/Simulate geography** of the world on the tileset based on configurable inputs

3. **Downsample the planet** to display to the player to approve generation

4. **Player picks starting point** or "landing zone" on the sphere. This will be coordinates on the planet/penta/sphere surface. This sample will not be the centroid of the pentagon. This will have an extremely high resolution.

5. **Generate starting 2D square chunk** based on the sample location. The chunk size will have no relation to the 3D tile size and may be smaller or larger (this will need to be configurable). The initial creation of the chunk will be created by sampling along the surface of the sphere. These chunks need to be large enough that the player is mostly playing on a single chunk. This is some arbitrary number based on how fast the entities move and the max scroll speed (or something like that). It should be around a minute or two to have an entity move across a single chunk.

6. **Sample the sphere** to populate the tiles in the chunk. There will need to be a transformation done between points on a sphere to a grid here. Details will be added to the various tile types such as flow direction of water, or snow depth where appropriate. This may just be a single terrain type, such as "grassland" or a couple such as "grassland", "river" and "ocean" but it will still be very high level. This should be done in a spiral around the edges of the chunk first. If they're all the same, that means that the entire chunk is the same surface! (time saver!)

7. **Use noise, randomness, or other techniques** to make the chunk more organic. Add detail to the chunk such as foliage. This will need to respect the edges of adjacent chunks if they have been generated. (TODO: how?)

8. **Render the visible part** of the chunk

9. **Generate adjacent chunks** through the sampling process as the player gets to them

## Sphere Subdivision

### Resources

- [Goldberg polyhedron - Wikipedia](https://en.wikipedia.org/wiki/Goldberg_polyhedron)
- [Euler characteristic - Wikipedia](https://en.wikipedia.org/wiki/Euler_characteristic)
- [Maps, Globes and Coordinate Systems](https://en.wikipedia.org/wiki/Map_projection)

### Fractal Philosophy: Icosahedron Approach

From "Fractal Philosophy: Maps: Fractals, Tectonics and the Fourth Dimension":

- **Icosahedron** - 20 equilateral triangles, evenly spaced (20 sided die). The largest even tiling of a sphere where all the spaces are equal

- **Fractal subdivision** of the icosahedron adds detail. Take the midpoint of each of the triangles, then draw more triangles. "Subdividing the spaces"

- **Taking the dual** - Take the midpoints of those triangles (taking the "dual") then connect those, you get a dodecahedron, 5-sided shapes, the second largest even tiling of a sphere.

- **Result** - The effect applies to the dual of the triangles. This creates pentagons filled with hexagons

- **Continued subdivision** - Subdividing more and more. There will still be 12 hexagons somewhere, you can find them in the Rimworld globe!
  - **Note:** How is this becoming spherical? I think the 2D illustration here is a bit hard to understand

- **Distortion** - If you apply some distortion to the original TRIANGLES, you get varied sizes
  - **Note:** How do you subdivide these random shapes?

- **Plate generation** - MORE ON PLATE GENERATION HERE

- **Plate movement** - Via Euler rotation theorem ([Euler's rotation theorem - Wikipedia](https://en.wikipedia.org/wiki/Euler%27s_rotation_theorem))
  - Need to understand "right hand rule" and "cross product"

## Pathfinding and Game AI

### Ray Marching
- [Coding Adventure: Ray Marching](https://www.youtube.com/watch?v=Cp5WWtMoeKg)

### Raycasting
- [Coding Challenge 145: 2D Raycasting - YouTube](https://www.youtube.com/watch?v=TOEi6T2mtHo)

## Video References

### Planet/World Generation
- [The Most Realistic Planet Simulator Yet](https://www.youtube.com/watch?v=lctXaT9pxA0) - Devote
- [Making an Actually Accurate Planet Generator](https://www.youtube.com/watch?v=sLqXFF8mlEU) - Devote
  - Has tip for sampling around the edges to create chunks
- [Coding Adventure: Procedural Moons and Planets](https://www.youtube.com/watch?v=QN39W020LqU)
- [Better Mountain Generators That Aren't Perlin Noise or Erosion](https://www.youtube.com/watch?v=gsJHzBTPG0Y)
- [Surface-Stable Fractal Dithering Explained](https://blog.demofox.org/2022/01/01/interleaved-gradient-noise-a-different-kind-of-low-discrepancy-sequence/)

### Game Development
- [7 Years of Indie Game Development - The Making Of Sapiens](https://www.gamedev.tv/blog/how-sapiens-was-made)

## Code and Libraries

### Source Code References
- [hexasphere.js](https://github.com/arscan/hexasphere.js) - Generate a sphere covered (mostly) in hexagons
- [earthgen-old](https://github.com/vraid/earthgen-old) - Old earthgen project

### Libraries
- **CesiumJS** - 3D globe and map library

## Related Technical Documents

- [World Generation Architecture](/docs/technical/world-generation-architecture.md) - Formal design for world generation system
- [Monorepo Structure](/docs/technical/monorepo-structure.md) - Project organization
- [Renderer Architecture](/docs/technical/renderer-architecture.md) - OpenGL rendering system
