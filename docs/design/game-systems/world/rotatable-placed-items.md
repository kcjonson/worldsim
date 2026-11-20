# Rotatable placed items

**Epic:** WOR-1131 (Freely rotatable placed assets: showing height at any angle)
**Technical spec:** [Rotatable placed items: technical design](../../../technical/rendering/rotatable-placed-items.md)

Furniture, workstations, storage, and anything else a colonist crafts and the player places turns to any angle and still shows its height. The view is RimWorld's (a top-down plan with a sliver of each object's front showing below its top), with one difference: RimWorld turns objects in quarter turns, and here an object turns freely, so a workbench can sit square to a wall drawn at 20 degrees or stand at an angle in the middle of a room.

The look is a rough simulation of a 3D object made of boxes, drawn with flat 2D art. Each item is a few upright blocks (parts). Each part has a drawing for its top and drawings for its sides. The top drawing turns with the item; each side drawing is laid onto the visible side of its block at a skew, the way a texture is laid onto a face, and shaded by a fixed light. One flat drawing of a side covers every angle.

## What the player sees

- A placed item's top is its plan, undistorted, turned to the item's angle.
- Below the top, the sides that face the viewer show, as tall as the item is. A side that faces away is hidden. A workbench square to the screen shows its front; turned 35 degrees it shows its front and its left end, each foreshortened; turned 180 degrees it shows its back.
- Sides are shaded by one light from the upper left, the same direction the flora art is lit from. A side facing left is brightest; a side facing straight down, or down and right, is darkest. Tops are not shaded.
- Every part has a dark outline of constant width around its silhouette, and a lighter line round its top. Lines in the side drawings keep their width when the side is skewed; a thin plank seam on a side seen almost edge-on is as thin as it is on a side seen front-on.
- Height reads the same everywhere. An item's height in metres sets how far its top rises up the screen, by one ratio shared with everything else that has height (walls, when they gain it), so a 0.8 m workbench never looks taller than a 2.4 m wall.
- Colonists and other items sort in front of or behind an item by where they stand relative to its footprint, along its whole length: a colonist standing at the front of a diagonal bench's left end draws in front of that end, and one standing behind its right end draws behind that end.

## Placing an item

Placing works the way the drawing tools do: it snaps by default, and holding **Alt** places it free.

1. Pick the item (from the Build menu, or **Place** on a crafted item waiting in its crate). A ghost of the item follows the pointer, at the turn last used for that item this game, or front-down the first time.
2. Turn it with the keyboard or the mouse.
   - **Q** turns it one step counter-clockwise, **E** one step clockwise.
   - Or press where the item goes and drag: the item stays where the press was and turns so its front faces the pointer. Release places it. A release within 4 screen pixels of the press is a click, which places it at the turn it shows.
3. The turn snaps, first that applies wins:
   1. **Square to a wall or a foundation edge.** Within 2 m of the item, the nearest wall face or foundation edge, built or blueprint, offers its direction and the three quarter turns from it. A turn within half an angle step (7.5 degrees) of one of those takes it.
   2. **The angle step.** Otherwise the turn rounds to a multiple of the angle step, 15 degrees, measured from front-down. The step is the drawing tools' angle step, one value for both.
   Q and E step to the next stop in their direction: the next multiple of 15 degrees, or a quarter turn of a nearby wall's direction if that comes first.
4. The position snaps flush. Squared to a wall, an item whose side comes within 0.3 m of that wall's face (the drawing tools' edge snap radius) slides so the side touches the face: a workbench backs onto the wall. The same holds for the face of another placed item it is square to, so a row of beds lines up side to side. Otherwise the item sits where the pointer is.
5. Hold **Alt** for no snapping: the turn is the raw drag direction, Q and E turn by 1 degree, and the item sits at the pointer. The ground rules still apply.
6. A click (or a release) where the item can't go places nothing; the ghost reads red: its whole footprint, at its turn, must be on buildable ground the colonists have seen.

**Esc** or right-click leaves placement. While placing, the key guide panel lists Q/E, drag to turn, and Alt.

A placed item keeps its turn. Moving an item (relocating it) starts the ghost at the item's current turn. A crafted item waiting to be installed shows its ghost at the chosen spot and turn until a colonist installs it there.

Colonists work at an item from its front: a workbench's crafter stands at the middle of its front, wherever the bench is turned. An item may name another spot.

## The art contract

This section is for whoever draws an item. The worked numbers are for the workbench.

### What an item is made of

An item is a few **parts**. A part is an upright block: a footprint (a rectangle, or a round with a given number of sides), a bottom height and a top height in metres, a drawing for its top, and drawings for its sides. Parts are placed inside the item; one part can be placed many times (a stool's four legs are one leg part placed four times, each with its own offset and turn). Parts belong to their item: there is no library of parts shared between items.

A box is one part. That is the simplest item, and every placed item, however plain, is drawn this way.

### Files

One SVG file per drawing, in the item's folder. A drawing used on several faces is one file. The item's XML says which file goes where; nothing about the item's structure lives in the SVG, so the files open and save cleanly in any vector program.

### Sizes

Drawings are authored at **64 pixels per metre**.

- A **top drawing** covers its part's footprint: a 3.0 m by 1.0 m slab's top is 192 by 64 pixels, its viewBox `0 0 192 64`, with the top-left corner of the drawing at the footprint's back-left corner. For a round part the drawing covers the round's bounding square.
- A **side drawing** is the face as seen straight on, flat: as wide as the face is long and as tall as the part is high. The workbench carcass front, 2.84 m long and 0.72 m high, is 181.76 by 46.08 pixels. The drawing's left edge is the face's left end as seen standing in front of that face; its top edge is the part's top.
- A round part's sides are one drawing, a strip as wide as one facet, repeated on every facet. A 12-sided round of radius 0.4 m has facets 0.207 m wide, so its strip is 13.25 pixels wide.

A drawing's size must match its face to within half a pixel, or the item fails to load. Two faces share a drawing only when they are the same size.

### Which faces need a drawing

The sides of a rectangle are named as someone standing in front of the item sees them: **front** (facing down the screen when the item is unturned), **back**, **left**, and **right**. Each side either has a drawing or is a plain colour. Draw a side when it carries detail the player should see (drawers, a firebox, shelves with goods); leave it plain when it is just material. A side that is never seen in practice (a wardrobe's back, if the player always puts it against a wall) still shows when the item is turned, so it gets at least its material colour.

A top with no drawing is a plain colour too. A top hidden under another part (a carcass under its worktop) is left plain.

### Light, colour, and lines

- **No baked light.** Every drawing turns with the item, so nothing in it may assume where the light is: no highlight on one edge, no shadow on another, no gradient that reads as light falling. The game shades each side by the direction it faces. Draw the material's own colour, flat, with its own markings (grain, joints, stitching). This holds for tops as well as sides.
- **Shading is applied over everything on a side**, fills and lines alike, so a side's art darkens as a whole when it turns from the light.
- **Outlines are the game's.** Don't draw a silhouette outline round a drawing; the game draws one round each part. Draw lines inside the art (plank seams, drawer edges) as strokes, at the width they should look; they stay that width on any skew.
- **What SVG may hold** is what the asset pipeline reads already: paths and the basic shapes, filled and stroked in colours or gradients. No filters, masks, clip paths, text, images, patterns, markers, `<use>`, style sheets, or dashed strokes.

### What a typical item costs

| Item | Parts (unique, placed) | Drawings | Notes |
|---|---|---|---|
| Workbench | 4 unique, 4 placed (carcass, worktop, vise, toolbox) | 12: 3 tops, 9 sides | The carcass is 3 sides (front, back, both ends as one); the worktop has a top and one drawing for both ends, its long edges plain; the vise and toolbox carry the rest |
| Workbench body alone | 2 (carcass, worktop) | 5 | RimWorld draws a bench as 3 textures (south, north, east, with west mirrored) |
| Stool | 2 unique (leg, seat), 5 placed | 1 (the seat top) | Legs and seat sides are plain wood |

Most furniture lands between the stool and the workbench: a body part with a front drawing and a top, plus a part or two for what sits on it.

## Out of scope

- **Flora.** Trees, bushes, and grass keep their billboard art and don't turn.
- **Ground shadows.** An item's parts carry the data a shadow needs (footprints and heights), and the shadows themselves are designed in WOR-1135 (Shadows: one soft-edge system for placed-item shadows, wall shadows, and sight edges).
- **How walls are drawn.** This spec sets the one height rule and the shared ratio; wall height and wall style belong to WOR-740 (Finish buildings: rotated-shape snapping, wall styling, enclosed state), which tunes the ratio and the walls together with these items.
- **Turning a placed item in place.** An item is turned while it is placed or relocated, not afterwards on its own.
- **Moving parts and animation.** Drawers that open, a bellows that pumps: parts are static.
- **Sloped or curved tops**, overhangs that hang below a neighbouring part's top, and parts that are not upright blocks.
- **Day and night light.** The light is fixed. It is one config value, so a later day and night system can move it without touching any asset.

## Addendum: decisions

A supplement, not part of the spec. Where it and the spec disagree, the spec is right. The owner's words are dated 2026-11-20.

- **The goal.** "We want the same view style as rimworld, but infinitely rotatable." The starting idea was a freely rotated SVG top, SVG sides, and side height data: "place the top then model the box and skew of the sides (polygon skew) then paint the side asset on at a skew as if it were a texture"; "we're essentially roughly simulating if it were a 3d object that could be rotated, but by skewing the assets as if they were textures."
- **Several blocks per item.** Chosen over one box per item with all the detail painted on its sides: "Parts is perfect. This is better than I expected."
- **Placed items only.** "not rotating flora. this is for placed items, furnature, workstations, crafted things."
- **Parts reused within one item.** "you have parts for the legs, but they're all essentially the same leg, so we wouldn't need unique assets for every leg. we should have some reusable part structure." Asked whether parts are shared across items or reused only within one: "Within one asset". Chosen over a shared part library.
- **One SVG per drawing.** Chosen over one SVG holding all of an item's drawings with structure in its metadata: "one per file I think would be easier to import and modify in vector programs because it wouldn't smash our unique structure or metadata, right?"
- **The height ratio below 0.5.** "I think it needs to be lower [than 0.5], because we'll need to get this visual style in line with wall height and size, it can't look like the station is taller than the walls. note: we have not worked on wall style yet, still need to do that, so its adjustable to tune the walls and assets together." The ratio is one config value, 0.35 to start, tuned with the walls under WOR-740.
- **Constant line widths.** Lines keep their width on skewed faces, over letting the skew thin them.
- **Snap by default, free on request.** "snap by default, free on request. just like walls." Read as: the turn snaps to nearby walls and to the drawing tools' 15 degree step, the position slides flush against a wall or an item it is square to, and Alt drops all of it, as Alt drops the drawing tools' snapping. The wall tool's angle steps are a helper that is off by default; a turn snaps by default, because an item at a stray 3 degrees reads as a mistake and there is no rectangle tool to fall back on.
- **Q and E, and drag to turn.** Q and E as RimWorld has them, for quick steps; press and drag for any angle, over a scroll wheel (the wheel zooms the camera) or a separate turn handle.
- **Fixed light from the upper left.** Matching the flora art, one config value so day and night can drive it later.
- **Shadows in, then out.** Simple ground shadows from footprint and height were first part of this epic. They moved out to their own epic: "well, we'll want to do the shadows the same place we do other edge smoothing like the sight cones and wall shadows. should be a single approach, especially since they'll need to stack and maybe even effect each other. perhaps shadows should be their own follow up epic". They are WOR-1135 (Shadows: one soft-edge system for placed-item shadows, wall shadows, and sight edges), which reads the same light value.
- **Left and right named from the front.** A rectangle's sides are named as a viewer standing in front of the item sees them, over the item's own left and right, so an author drawing the "left end" draws what is on their left.
- **Work from the front.** A crafter stands at the middle of an item's front by default, over the nearest open point anywhere round it, so a turned bench is worked from the side its drawers face.
