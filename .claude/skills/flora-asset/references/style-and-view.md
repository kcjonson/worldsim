# Style and view

Match the game's existing flora, not generic flat-vector art. The look: cel-shaded, limited muted-earthy palette, strong readable silhouette, organic (not geometric) edges, layered tones for depth.

## Style statement (the target — keep this in spirit on every asset)

Hand-painted 2D game-asset illustration of plants/foliage for a top-down colony sim (RimWorld-ish). Stylized, no photorealism. Soft cel-shading in two or three flat tonal steps PLUS gentle ambient occlusion in the recesses where forms meet. Soft, irregular, organic edges — never geometric or hard-cornered — with a strong readable silhouette. Foliage reads as a handful of DEFINED LEAF-CLUSTER shapes (lobes/clumps with their own light and shadow), NOT individual leaves and NOT a single smooth dome (a plain dome reads too simplistic). A THIN darker rim, a few px wide, around the form. Muted, earthy, slightly desaturated palette. Light consistently from the upper-left. Single centered object, no ground, no cast shadow. Trees: a couple of branches may show through gaps in the canopy.

## Compose masses FROM shapes — not a background with details painted on top

THE most common failure, avoid it: drawing one smooth canopy blob and then painting clusters, highlights, and dark spots on top of it. That always reads flat and simplistic no matter how much you pile on. Complexity is not the amount of stuff added to a background — it is the structure of the form itself.

A canopy (and any foliage mass) IS its cluster shapes:
- Build the canopy ONLY from a set of overlapping leaf-cluster shapes (roughly 8-15 for a tree). Their UNION is the canopy; the lumpy organic silhouette comes from the edge clusters poking out at different distances. There is NO separate canopy-body / silhouette fill underneath them.
- Each cluster is a COMPLEX ORGANIC shape, never a circle or ellipse. Perfect ovals read as a pile of balls or a bunch of grapes. Give every cluster an irregular, lumpy, slightly different outline (Bezier `path`, not `<circle>`/`<ellipse>`), and shape the clusters to evoke the species: rounded multi-lobed clumps for an oak, broader slightly-pointed lobes for a maple, small airy tufts for a birch, dense rounded masses for a beech, spiky fans for a conifer.
- Cel-shade EACH cluster as its own small mass with flat tonal steps: a darker base, a mid tone, a lighter cap offset toward the upper-left light. Keep the cap an organic shaded region that echoes the cluster's own outline, not a concentric bullseye.
- Draw clusters BACK-TO-FRONT (far/lower ones first, near/upper last). A nearer cluster's darker base overlapping the one behind it is what creates the ambient-occlusion seams between clumps — the depth comes from the overlaps, not from painted-on shadow spots.
- One thin dark rim around the whole union only (draw expanded dark copies of the cluster blobs behind everything), never a rim per cluster.
- The canopy must be SOLID: overlap the clusters enough that the background shows ONLY outside the silhouette, never as holes or gaps punched through the inside of the canopy. Some ragged edge is fine; internal sky-holes are not. (An airy species like birch can have a slightly looser edge, but still no holes through the middle.)
- Backing fallback (belt-and-suspenders against holes): behind the cluster fills (drawn right after the rim, before the first cluster) lay ONE solid silhouette blob filling the canopy interior in the DARKEST foliage tone, inset just inside the dark rim so the rim still reads as the outer border. This is NOT the construction shape and you never draw detail on it, the clusters still form the canopy; it only shows through an accidental gap, so a stray hole reads as deep foliage shadow instead of sky. This matters most for the procedural Lua generator, where per-instance jitter can open gaps a static SVG wouldn't have.
- The dark outline is MANDATORY and must READ clearly: a clean dark border a few px wide tracing the whole canopy union (the expanded dark cluster copies behind everything). Keep it thick enough to see, never so thin it disappears. This dark border is a defining part of the look, every asset has it.
- Connect the canopy to the trunk: draw the short trunk stub FIRST, then let the lowest center clusters overlap the top of the trunk so there is no gap. A canopy floating above a detached trunk is wrong.
- Shade the whole canopy as a DOME seen from above: clusters toward the top-center-left (facing the light) get the brightest caps; clusters toward the lower and far edges stay darker, smaller-capped. This dome falloff is what makes it read as a 3/4 top-down view rather than a flat front-on bush.

If you find yourself drawing a big silhouette shape first and then adding bumps to it, stop, you have it inside-out.

### What a cluster shape looks like (concrete)

A cluster outline is a CLOSED path with 6-9 outward rounded bumps around its perimeter, like a cloud, a head of broccoli, or a tight bunch of leaves. It is NOT a smooth oval. Build each from cubic Beziers where every segment bulges outward to make one lobe; vary the bump count and size per cluster so no two match. The canopy's scalloped edge then comes for free from the edge clusters' bumps. A wobbly oval (4-6 smooth segments) is the failure mode, it reads as a ball.

Example, one cluster as base + offset cap, light from upper-left:

```xml
<!-- dark base: ~8 outward lobes -->
<path d="M28,48 C24,40 30,31 39,33 C41,25 52,24 56,31 C64,27 73,32 71,42 C79,44 81,53 73,58 C77,67 67,73 59,68 C56,76 44,76 41,68 C32,71 24,65 27,56 C20,54 21,47 28,48 Z" fill="#2f4a26"/>
<!-- lighter cap: SMALLER, same lumpy family, shifted UP-LEFT (not concentric) so the base shows as shadow lower-right -->
<path d="M34,42 C31,36 37,30 44,32 C47,27 54,28 56,34 C62,32 67,37 64,44 C61,50 53,52 48,48 C43,53 36,50 34,42 Z" fill="#4f7536"/>
```

The cap is NEVER a concentric ring centered on the base, that reads as a shiny eyeball. Offset it toward the light and let it echo the base's lumpy edge. Match the lobing to the species (rounded notched lobes for oak, pointier for maple, small fine tufts for birch).

## Layered cel-shade construction (every part)

No ground shadow — the game doesn't use them. Build each mass from back to front in tonal steps:
1. **Outline** — a clean dark border around the whole mass. Put an SVG `stroke` (+`stroke-width`) on the shape; the renderer tessellates strokes now. A group `stroke` outlines each child path, not the union, so to get ONE outline around many overlapping shapes, draw an expanded dark copy of the silhouette behind them instead.
2. **Dark rim** — the part's silhouette in the darkest tone (base x ~0.7).
3. **Base** — the main mid tone, slightly inset from the rim.
4. **Mid** — lighter, biased up-and-left toward the light.
5. **Highlight** — smallest, lightest, same upper-left bias.

Light comes from the upper-left, consistently. The view is 3/4 top-down (see below), never a flat side elevation.

## Per-subject view rules

The engine's canonical view is 3/4 top-down (see `deciduous.lua`: "canopy viewed from above, trunk visible extending downward"). Apply per growth form:

- **Trees** — 3/4 top-down, leaning OVERHEAD: you look DOWN onto the canopy. The canopy is the dominant mass and reads as a rounded crown seen from above (a roughly round, full footprint, NOT a thin flat band on a stick and NOT a tall side-on bush), dome-shaded so the lit top surface faces up-and-left. The trunk is only a SMALL stub barely peeking below the canopy's front edge, the canopy overlaps and hides most of it. Light top-center-left, darker lower/far rim.
  - **Limbs are THICK.** Where a limb shows (at the trunk-canopy join or through a gap) draw it chunky and tapering, roughly half to two-thirds the trunk's width at its base, never a thin stick. A big tree has substantial limbs; sticks read as twigs against the canopy mass.
  - **Conifers** may keep a slightly PEAKED crown (mild verticality is allowed for evergreens) while broadleaves stay domed.
- **Shrubs / bushes** — rounded mound seen slightly from above. Little or no trunk. Flatten with `stretchY < 1`.
- **Ground plants / crops / ferns / flowers** — top-down. Leaves/fronds radiate from a center point. A flower adds a small bright corolla cluster at the center on top.
- **Grasses** — a few tapered blades from a base point (see `assets/world/flora/GrassBlade/blade.svg`); thin, curved, pointed.

## Species cheat-sheet (distinct species WITHIN the 3/4 top-down view)

Every tree uses the locked 3/4 top-down foreshortened dome (see the Trees view rule): you look DOWN on the canopy with a short trunk stub below, shaded like a dome. You do NOT see a side profile, so a species' real-world height and side-silhouette (columnar, conical, weeping curtains) DO NOT show. Height is only the `worldHeight` value, never a taller picture. A column, a Christmas-tree triangle, a lollipop-on-a-stick, or hanging curtains all mean you've slipped into side elevation, pull back to a canopy seen from above.

Differentiate species by what you CAN see from above:
- **Palette / colour** — the strongest lever.
- **Footprint** size and shape — big vs small; round vs slightly oval vs ragged.
- **Lobe / leaf texture** — rounded, pointed-star, fine tufts, spiky.
- **Density** — packed vs open with sky-gaps.

- **English oak** — large, broad, round, dense canopy; rounded notched lobes; deep cool green (`#2f4a26 #3e5e2c #4f7536 #6f9a4a`). The default big shade tree. worldHeight ~4.
- **European beech** — large dense round canopy, smooth rounded lobes, rich warm mid-green (warmer than oak). worldHeight ~4.
- **Silver birch** — smaller, OPEN and airy canopy with sky-gaps between loose clumps; fine small tufts; light yellow-green (`#38491f #4f6a2a #6c8a36 #8fab4f`); pale flecked trunk. worldHeight ~4.5.
- **Japanese maple** — smaller round canopy, fine POINTED star-lobe texture, muted burgundy-bronze (`#4a2620 #6e3528 #934636 #b25f45`). Distinct by colour + pointed lobes. worldHeight ~2.5.
- **Aspen / poplar** — medium, TIGHTER, rounder canopy (smaller footprint than oak) built from many small clumps; warm medium green (`#3a4f1e #506e28 #6e9134 #8fb24c`). A tall trunk only sets worldHeight, not the picture. worldHeight ~5.
- **Pine / spruce (conifer)** — the one species allowed mild verticality: a rounded crown that comes to a soft PEAK at the top-center, built from layered, overlapping, slightly drooping spiky fan-clumps (dense and cohesive, NOT a scattered rosette of separate spikes). Dark blue-green (`#1e3528 #2b4a35 #3a5e44 #4f7555`). Reads as an evergreen seen from a high 3/4 angle, peaked but still mostly top-down, not a flat side-view Christmas triangle. worldHeight ~5.5.
- **Willow** — broad round canopy with a soft, shaggy, RAGGED rim (fine trailing tips around the edge only), soft yellow-green (`#4a5a22 #6a7d2e #8a9d3e #acbd5a`). No long hanging curtains. worldHeight ~4.

Rule of thumb: change the colour, footprint, and texture, never the camera.

## Palette (muted, earthy)

0..1 floats for Lua, hex for SVG. Keep one hue family per part; derive rim = base x 0.7, highlight = base x ~1.25.

Foliage, temperate green:
- edge `0.18,0.29,0.15` `#2f4a26`
- base `0.24,0.37,0.17` `#3e5e2c`
- mid `0.31,0.46,0.21` `#4f7536`
- highlight `0.44,0.60,0.29` `#6f9a4a`

Bark, brown:
- shadow side `0.27,0.19,0.11` `#45301d`
- base `0.35,0.25,0.15` `#5a3f26`
- light side `0.45,0.32,0.18` `#73512f`

Variants (swap the foliage hue, keep structure):
- Autumn gold: edge `#7a4416` base `#9c5a1c` mid `#c47a26` hi `#e0a23a`
- Dead / winter: edge `#5b5340` base `#6e6450` mid `#837861` hi `#9a8f76`
- Arid sage: edge `#4a5237` base `#5e6845` mid `#76805a` hi `#94a075`

Fruit/berry accents: red `#c41e1e` / `0.77,0.12,0.12`, blue `#3a5a8a`, orange `#d4762a`.

Ground shadow: `#1d2616` at ~0.22-0.30 alpha (slightly green-black, not pure grey).

## Life stages

Scale the same construction across growth:
- Seedling: ~20-30% size, sparse canopy (skip the mid/highlight layers), thin stem.
- Sapling: ~50-70% size, fuller but still open.
- Mature: full size and full layering.
- Dead / stump: trunk only, or a bare branch silhouette in the Dead palette.

Drive this from a `growthStage` param (scale radius/height, gate canopy-layer count and fruit). Pair with the seasonal palettes above for a full stage x season set.

## SVG template (simple assets)

Static hex fills, closed paths, semantic groups, light from upper-left. Renderable today. Tune the viewBox to the subject; the engine normalizes height to `worldHeight`.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 90" width="100" height="90">
  <g id="body">
    <path d="M50,18 C74,18 88,32 88,50 C88,66 72,78 50,78 C28,78 12,66 12,50 C12,32 26,18 50,18 Z" fill="#2f4a26" stroke="#1b2912" stroke-width="2.5"/>
    <path d="M50,22 C71,22 84,34 84,50 C84,64 70,75 50,75 C30,75 16,64 16,50 C16,34 29,22 50,22 Z" fill="#3e5e2c"/>
    <path d="M44,28 C62,26 76,36 76,50 C76,62 62,68 46,66 C30,64 22,52 26,40 C29,32 36,29 44,28 Z" fill="#4f7536"/>
    <path d="M42,32 C54,30 64,38 62,48 C60,58 48,60 38,56 C30,52 30,40 42,32 Z" fill="#6f9a4a"/>
  </g>
</svg>
```

For a tree as simple SVG, add a `#trunk` group (two brown tones) below a `#canopy` group, with the canopy overlapping the trunk top so the join is hidden. Prefer the procedural Lua path for trees, though, so they vary per instance.

## Recolor / variation

Runtime variation lives in the **Lua** generator (jitter the palette base per variant, vary radii/counts), as `deciduous.lua` does. Static SVG fills are baked; for a static asset that needs a few distinct looks, author separate variant SVGs rather than expecting runtime recolor.

(If a future renderer change adds CSS-variable color slots to SVG, the layered groups above map cleanly onto per-part slots: one slot per group, defaults = the base tone, engine drives the rim/mid/highlight as ratios. Not implemented yet; don't ship assets that depend on it.)

## Gradients

Gradient fills render (linear and radial), baked to per-vertex colors at load. Use them for smooth shading the flat-tone layering only approximates: canopy depth, soft ambient occlusion, leaf sheen.

- Use `gradientUnits="userSpaceOnUse"` with explicit coordinates. objectBoundingBox (the SVG default) renders flat — NanoSVG treats its 0..1 fractions as pixels — so always set `userSpaceOnUse` and real coords matching the shape.
- Linear, radial, and multi-stop all work. Radial fills get an inserted centroid sample so the center shows (the perimeter would otherwise be one flat edge color).
- A gradient still tessellates as the shape's polygon; keep gradient shapes convex-ish for the cleanest radial center.
- Live reference: the ui-sandbox `svg` scene (`assets/svg/showcase.svg`) exercises solid, linear, radial, multi-stop, the shape primitives, opacity, layering, and gradients on non-circle shapes.

## Tessellation discipline

- Closed paths, ≥3 vertices. Self-intersection is now handled by the sweep-line tessellator, but clean non-crossing blobs (single monotonic angle sweep) still tessellate fastest.
- Strokes render: `stroke` + `stroke-width` on a path become a tessellated outline band (use them for the dark cel outline). A group stroke outlines each child, not the union — for one outline around many overlapping shapes, draw an expanded dark silhouette behind them.
- Keep node counts modest. At game zoom, 24 segments per canopy blob is plenty; more just muddies and costs tessellation.
