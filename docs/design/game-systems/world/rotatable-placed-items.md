# Rotatable placed items

**Epic:** WOR-1131 (Freely rotatable placed assets: showing height at any angle)
**Technical spec:** [Rotatable placed items: technical design](../../../technical/rendering/rotatable-placed-items.md)

Furniture, workstations, storage, and anything else a colonist crafts and the player places turns to any angle and still shows its height. The view is RimWorld's (a top-down plan with a sliver of each object's front showing below its top), with one difference: RimWorld turns objects in quarter turns, and here an object turns freely, so a workbench can sit square to a wall drawn at 20 degrees or stand at an angle in the middle of a room.

The look is a rough simulation of a 3D object made of boxes, drawn with flat vector art. Each item is a few upright blocks (parts). Each part has a drawing for its top and drawings for its sides. The top drawing turns with the item; each side drawing is laid onto the visible side of its block at a skew, the way a texture is laid onto a face, and shaded by a fixed light. One flat drawing of a side covers every angle.

## What the player sees

- A placed item's top is its plan, undistorted, turned to the item's angle.
- Below the top, the sides that face the viewer show, as tall as the item is. A side that faces away is hidden. Turns are clockwise on the screen. A workbench square to the screen shows its front; turned 35 degrees clockwise it shows its front and its right end, each foreshortened; turned 35 degrees the other way, its front and its left end; turned 180 degrees, its back.
- Sides are shaded by one light from the upper left, the same direction the flora art is lit from. A side facing left is brightest; a side facing straight down, or down and right, is darkest. Tops are not shaded.
- Every part has a dark outline of constant width around its silhouette, and a lighter line round its top. Lines in the side drawings keep their width when the side is skewed; a thin plank seam on a side seen almost edge-on is as thin as it is on a side seen front-on. A side seen so nearly edge-on that its detail would be a smear shows its plain colour instead.
- Height reads the same everywhere. An item's height in metres sets how far its top rises up the screen, by one ratio shared with everything else that has height, walls included, so a 0.8 m workbench never looks taller than a 2.4 m wall.
- Colonists and other items sort in front of or behind an item by where they stand relative to its footprint, along its whole length: a colonist standing at the front of a diagonal bench's left end draws in front of that end, and one standing behind its right end draws behind that end.
- Zoomed out, an item drops the fine detail its artist marked as fine (grain, stitching, small tools), as flora and the ground do, so a far view of a full workshop stays cheap.
- An item no colonist can stand to use (walled in, or its working side blocked) shows a blocked marker, and its info panel says nowhere to stand.

## Using an item

A colonist uses an item from a **use area**: a patch of ground in the item's frame that turns and moves with it. An item has one or many. A storage box is used from anywhere round it; a smelter only from a patch in front of its mouth; a long counter could be worked from either of two patches along its front.

A colonist walks to the nearest point of a use area he can reach and stand on, and works from there. Ground inside the item itself is never a place to stand. An item that names no use area is used from anywhere within 0.7 m of its body, the construction pathing clearance.

An item also has a **clearance**: space round its body that other items' bodies and walls stay out of. A box has none and can sit against another box. A bed might keep a strip clear down one side so it can be got into; a forge might keep space round its hot sides. Clearance is walkable, it is not a wall, and two items' clearances may overlap: two benches facing each other can share one aisle.

Use areas and clearance follow the item when it moves. Placement checks them; after placement, if walls or other items later take every spot in its use areas, the item shows it is blocked (above). Which items a colonist knows about, and which he can reach, follow the same rules as everything else he works with.

## Placing an item

Placing works the way the drawing tools do: it snaps by default, and holding **Alt** drops the snaps that are about style and keeps the ones that are about fitting against what is there.

1. Pick the item (from the Build menu, or **Place** on a crafted item waiting in its crate). A ghost of the item follows the pointer, at the turn last used for that item this game, or front-down the first time. The ghost shows the item's use areas and clearance on the ground beneath it.
2. Turn it with the keyboard or the mouse.
   - **Q** turns it one step counter-clockwise, **E** one step clockwise.
   - Or press where the item goes and drag: the item stays where the press was and turns so its front faces the pointer. Release places it. A release within 4 logical pixels of the press (`doubleClickDistancePx`, the drawing tools' click distance) is a click, which places it at the turn it shows.
3. The turn snaps, first that applies wins:
   1. **Square to a wall or a foundation edge.** Within 2 m of the item, the nearest wall face or foundation edge, built or blueprint, offers its direction and its four quarter turns (the direction itself and 90, 180, and 270 degrees from it). A turn within half an angle step (7.5 degrees) of one of those takes it.
   2. **The angle step.** Otherwise the turn rounds to a multiple of the angle step, 15 degrees, measured from front-down. The step is the drawing tools' angle step, one value for both.
   Q and E step to the next stop in their direction: the next multiple of 15 degrees, or a quarter turn of a nearby wall's direction if that comes first.
4. The position snaps flush. An item with a side parallel to a wall face, or to a side of another placed item, and within 0.3 m of it (the drawing tools' edge snap radius) slides until it touches: a workbench backs onto the wall, and a row of beds lines up side to side. A side with clearance stops that far off instead of touching. Otherwise the item sits where the pointer is.
5. Hold **Alt** to turn freely: no square-to-a-wall, no angle step, and Q and E turn by 1 degree. The flush slide stays, as the wall tools keep their snaps to existing corners and edges under Alt, so a freely turned item that happens to be parallel to a wall still backs onto it. The ground rules still apply.
6. A click (or a release) where the item can't go places nothing; the ghost reads red and says why, in a word or two beside it: the ground isn't buildable or hasn't been seen, it overlaps another item, it is too close to an item or a wall for one of their clearances, or no use area has room to stand.

The turn snaps by default even though the drawing tools' angle-step helper starts off. A building drawn with the helpers off still lands square, because the rectangle tool's grid does that job; an item has no grid, and an item at a stray 3 degrees reads as a mistake, so the turn step does the grid's job here and applies whatever the helper setting.

**Esc** or right-click leaves placement. While placing, the key guide panel lists Q/E, drag to turn, and Alt.

A placed item keeps its turn. Moving an item (relocating it) starts the ghost at the item's current turn. A crafted item waiting to be installed shows its ghost, with its use areas and clearance, at the chosen spot and turn until a colonist installs it there.

## The art contract

This section is for whoever draws an item. The worked numbers are for the workbench.

### What an item is made of

An item is a few **parts**. A part is an upright block: a footprint (a rectangle, or a regular polygon given by its side count and its circumradius), a bottom height and a top height in metres, a drawing for its top, and drawings for its sides. Parts are placed inside the item; one part can be placed many times (a stool's four legs are one leg part placed four times, each with its own offset and turn). Parts belong to their item: there is no library of parts shared between items.

A box is one part. That is the simplest item, and every placed item, however plain, is drawn this way.

Parts stand side by side or stack. Two parts whose footprints overlap must be separated in height (a worktop on its carcass); the higher draws over the lower. Two parts whose footprints don't overlap draw nearer-in-front, whatever their heights. An item whose parts overlap both in plan and in height fails to load.

### Files

One SVG file per drawing, in the item's folder. A drawing used on several faces is one file. The item's XML says which file goes where; nothing about the item's structure lives in the SVG, so the files open and save cleanly in any vector program.

### Sizes

Drawings are fully vector and have no fixed resolution. Draw at whatever scale is comfortable; what matters is the proportions.

- A **top drawing** covers its part's footprint: its viewBox maps onto the footprint's bounding box, top-left to the back-left corner. A 3.0 m by 1.0 m slab's top is a drawing three times as wide as it is tall (`0 0 300 100`, or `0 0 3 1`, both work). For a round part the viewBox covers the bounding box of its corners, which is square only when the side count is a multiple of 4.
- A **side drawing** is the face seen straight on, flat: its viewBox maps onto a rectangle as wide as the face is long and as tall as the part is high. The workbench carcass front, 2.84 m long and 0.72 m high, is a drawing 3.94 times as wide as it is tall. The drawing's left edge is the face's left end as seen standing in front of that face; its top edge is the part's top.
- A round part's sides are one drawing, a strip as wide as one facet, repeated on every facet. A 12-sided round of radius 0.4 m has facets 0.207 m wide and 0.5 m high (if the part is): a drawing 0.414 times as wide as it is tall.

A drawing's proportions must match its face to within 1%, or the item fails to load. Two faces share a drawing only when they have the same proportions; the drawing stretches to each.

Line widths in a drawing scale with the drawing: a stroke one hundredth of a 3 m face's width is 3 cm wide on the face.

### Detail levels

Mark fine detail with `data-lod`, as on ground and flora art: an element, or a `<g>` round several, with `data-lod="40"` is drawn only when the screen shows at least 40 pixels a metre (the game's zoom runs about 2 to 160, 24 by default). Unmarked shapes always draw. Put grain, stitching, nail heads, and the small things on a shelf under a `data-lod`; keep the shapes that say what the item is unmarked. An item lists the zooms it is built for with `<detailLevels>`, as flora does.

### Which faces need a drawing

The sides of a rectangle are named as someone standing in front of the item sees them: **front** (facing down the screen when the item is unturned), **back**, **left**, and **right**. Each side either has a drawing or is a plain colour. Draw a side when it carries detail the player should see (drawers, a firebox, shelves with goods); leave it plain when it is just material. A side that is never seen in practice (a wardrobe's back, if the player always puts it against a wall) still shows when the item is turned, so it gets at least its material colour.

A top with no drawing is a plain colour too. A top hidden under another part (a carcass under its worktop) is left plain.

### Use areas and clearance

Each is a set of shapes in the item's plan, in metres, written in the XML or drawn, mixed freely:

- In the XML: all round the item within a distance (the default use area), a circle at a point (a place to stand), or a rectangle.
- Drawn: an SVG in the item's folder whose filled shapes are the area. Its viewBox maps onto a rectangle the XML gives in the item's frame, so draw the area over a sketch of the item's plan at whatever scale suits. Strokes are ignored, so an outline of the item kept in the file as a guide doesn't count.

Clearance has a shorthand, one number for a margin all round the body; zero, the default, is none.

### Light, colour, and lines

- No baked light. Every drawing turns with the item, so nothing in it may assume where the light is: no highlight on one edge, no shadow on another, no gradient that reads as light falling. The game shades each side by the direction it faces. Draw the material's own colour, flat, with its own markings (grain, joints, stitching). This holds for tops as well as sides.
- Shading is applied over everything on a side, fills and lines alike, so a side's art darkens as a whole when it turns from the light.
- Outlines are the game's. Don't draw a silhouette outline round a drawing; the game draws one round each part. Draw lines inside the art (plank seams, drawer edges) as strokes, at the width they should look; they stay that width on any skew.
- Art stays inside its viewBox. A side's art is cut to its face, so anything past the edge is lost.
- What SVG may hold is what the asset pipeline reads already: paths and the basic shapes, filled and stroked in colours or gradients. No filters, masks, clip paths, text, images, patterns, markers, `<use>`, style sheets, or dashed strokes.

### What a typical item costs

| Item | Parts (unique, placed) | Drawings | Notes |
|---|---|---|---|
| Workbench | 4 unique, 4 placed (carcass, worktop, vise, toolbox) | 12: 3 tops, 9 sides | The carcass is 3 sides (front, back, both ends as one); the worktop has a top and one drawing for both ends, its long edges plain; the vise and toolbox carry the rest |
| Workbench body alone | 2 (carcass, worktop) | 5 | RimWorld draws a bench as 3 textures (south, north, east, with west mirrored) |
| Stool | 2 unique (leg, seat), 5 placed | 1 (the seat top) | Legs and seat sides are plain wood |

Most furniture lands between the stool and the workbench: a body part with a front drawing and a top, plus a part or two for what sits on it. Use areas and clearance are usually a line of XML each, or nothing.

## Out of scope

- Flora. Trees, bushes, and grass keep their billboard art and don't turn.
- Ground shadows. An item's parts carry the data a shadow needs (footprints and heights), and the shadows themselves are designed in WOR-1135 (Shadows: one soft-edge system for placed-item shadows, wall shadows, and sight edges).
- How walls are drawn. This spec sets the one height rule and the shared ratio; wall height and wall style belong to WOR-740 (Finish buildings: rotated-shape snapping, wall styling, enclosed state), which tunes the ratio and the walls together with these items.
- Turning a placed item in place. An item is turned while it is placed or relocated, not afterwards on its own.
- Moving parts and animation. Drawers that open, a bellows that pumps: parts are static.
- Sloped or curved tops, parts that are not upright blocks, and one item standing under another (a chair pushed under a desk). An item's body is the hull of its parts, so nothing stands in the space under a desk or in the notch of an L-shaped item.
- Day and night light. The light is fixed, one config value, and nothing in an asset depends on where it is.

## Addendum: decisions

A supplement, not part of the spec. Where it and the spec disagree, the spec is right. It holds the owner's decisions; the owner's words are dated 2026-11-20.

- The goal: "we want the same view style as rimworld, but infinitely rotatable." The starting idea was a freely rotated SVG top, SVG sides, and side height data: "place the top then model the box and skew of the sides (polygon skew) then paint the side asset on at a skew as if it were a texture"; "we're essentially roughly simulating if it were a 3d object that could be rotated, but by skewing the assets as if they were textures."
- Several blocks per item, chosen over one box per item with all the detail painted on its sides: "Parts is perfect. This is better than I expected."
- Placed items only: "not rotating flora. this is for placed items, furnature, workstations, crafted things."
- Parts reused within one item: "you have parts for the legs, but they're all essentially the same leg, so we wouldn't need unique assets for every leg. we should have some reusable part structure." Asked whether parts are shared across items or reused only within one: "Within one asset".
- One SVG per drawing, chosen over one SVG holding all of an item's drawings with structure in its metadata: "one per file I think would be easier to import and modify in vector programs because it wouldn't smash our unique structure or metadata, right?"
- The height ratio below 0.5, tuned with the walls: "I think it needs to be lower [than 0.5], because we'll need to get this visual style in line with wall height and size, it can't look like the station is taller than the walls. note: we have not worked on wall style yet, still need to do that, so its adjustable to tune the walls and assets together."
- Constant line widths: lines keep their width on skewed faces, rather than letting the skew thin them.
- Snapping: "snap by default, free on request. just like walls."
- A fixed light from the upper left, matching the flora art.
- Shadows moved to their own epic: "well, we'll want to do the shadows the same place we do other edge smoothing like the sight cones and wall shadows. should be a single approach, especially since they'll need to stack and maybe even effect each other. perhaps shadows should be their own follow up epic". They are WOR-1135 (Shadows: one soft-edge system for placed-item shadows, wall shadows, and sight edges).
- Fully vector, no pixel sizes, detail by level-of-detail tags: "we don't talk pixels here mate. assets can get as detailed as they want to be and have LOD tags. we already have this in code. these assets are fully vector. no raster anywhere".
- Use areas, one or many an item: "usage location. this should be one or many painted/defined areas that the asset can be accessed from or used from. a storage box would essentially just have a circle around it, but the smelter could only be used right in front of it."
- Clearance: "like usage location, but slightly different, clearance requirements. a box can be be placed right against another, but other items might need a little bit of space around them, either visually or functionally."
- Use areas and clearance as shapes, written in the XML or drawn, and shown in the asset manager: "the view/clearance areas are essentially shapes, we can either define them in the def XML OR have them as complex shapes like a part in the svg. I think for authoring large complex things an artist may want to draw and scale the shapes in there vector editor. but for simple things like a place to stand a circle at a point would be fine. in our asset viewer app, we'll need to see them visually."
