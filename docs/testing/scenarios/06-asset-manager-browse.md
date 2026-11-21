# Scenario 06: asset-manager browse

**Status:** draft  
**Last verified:** never

asset-manager on salal's ui: the tree, search, selection and the detail pane.

---

## Steps

Launch `asset-manager`; the dev server is on its usual port.

1. Look at the window: a top bar with search, Reload and a summary (`N assets - E errors, W warnings`), a tree of categories on the left, each row with its asset drawn small beside the name.
2. `/api/ui/lint`: `"count":0`.
3. Click a category header: its rows collapse and the chevron points right; click again to expand.
4. Click an asset row: the row shows selected and the right pane fills with its kicker, name, badge, preview and XML. The preview shows the asset, not an empty frame, with the collider outlined in cyan over it, or "no collider" in its corner.
5. Pick a procedural asset: six variant previews under Randomize, every one drawn; click Randomize and the previews change.
6. Pick Groundcover_Grass, whose XML is long: the top bar keeps its height with search and Reload inside it, and `/api/ui/lint` is `"count":0`.
7. Type part of a name in search: the tree narrows to matching assets.
8. Scroll the XML well and the tree: both scroll on their own, the XML sideways too, since its lines don't wrap.
9. Click Reload: the summary and tree rebuild, the selection kept when it still exists.
10. Close asset-manager, delete `%LOCALAPPDATA%\kcjonson\world-sim\cache\ground\` (the game's ground bake cache, which asset-manager shares) and launch it again. Select Ground_Grass and pick Bake: the log says `no bake of Ground_Grass under its current key`, the status counts through generating and baking the variants, then the pane shows the mips, the zoom strip and the report; the log's `Ground bake Ground_Grass` line gives the time and the paths, render and filter split.
11. Close and relaunch, open Ground_Grass's Bake again: it shows at once, and the log line says `from the cache`.
12. Add a comment line to `assets/shared/scripts/ground_grass.lua`, click Reload: the log says there's no bake of Ground_Grass under its current key, and it bakes again; the cache folder now holds two `Ground_Grass-` files. Take the comment out and click Reload: Ground_Grass's Bake shows at once, from the first file. Keep the file's line endings when editing (a Windows checkout with `core.autocrlf` has CRLF): the key hashes the script's bytes, so an editor that rewrites them as LF makes a third key and a fresh bake.

## Parts items

Preconditions: asset-manager running with World selected and the search box empty. `TestPartsAreas` (`assets/world/misc`) has three named use areas, `front` an XML rect, `side` drawn, and `back` two separate circles, and a clearance of a 0.1 m margin and a drawn apron clear of it; `TestPartsBox` has detail levels 8, 24 and 80 px/m with corner dots only at 80; the Workbench is under Sandbox.

13. Type `TestParts` in search: the tree shows TestPartsAreas and TestPartsBox, the box's row drawn front-down.
14. Click TestPartsBox: the badge reads PARTS in the same grey as SIMPLE, and the pane shows the turn controls, the level picker at 80 px/m, the overlay toggles, a large view, and 24 small views labelled 0 to 345.
15. Click `8 px/m`: the corner dots go from the large view and every small one, and the right column reads `level  8 px/m`.
16. Click `80 px/m`: the corner dots come back and the right column reads `level  80 px/m, the finest`.
17. Click TestPartsAreas: under the box lie three green areas, `front`, `side` and `back` (two circles), each name on a dark plate inside its area.
18. Look at the clearance: the apron before the box and the margin round it are two hatched pieces with dashed edges, and the cyan collider outlines the box.
19. Click `+15`: the entry reads 15.00 and the large view and its overlays turn clockwise together.
20. Click the entry, replace its text with `271` and press Enter: the view shows the left end and a sliver of the front.
21. Replace the entry's text with `12abc` and press Enter: the entry goes back to 271.00.
22. Click the large view, then press Right: 286.00.
23. Press Left: 271.00.
24. Click the Clearance checkbox, then press Right: the turn stays 271.00.
25. Click the small view labelled 90: the entry reads 90.00 and the large view matches it.
26. Untick Use areas: the green areas go from the large view and every small one.
27. Tick Footprints: the box's footprint is outlined in violet.
28. Read the right column's USE AREAS AND CLEARANCE: `front` reads `rect -0.6, 0.4 to 0.6, 1.2 m`, `side` reads `drawing side-use.svg over ...`, `back` two `circle` lines, and `clearance` reads `margin 0.1 m` and `drawing clear.svg over ...`.
29. Read DRAWINGS: `clear.svg` and `side-use.svg` each with what it maps onto, its size in metres, and `ok` with its proportions in green.
30. Read VALIDATION: `box` and `item` read `no issues`.
31. In `clear.svg`, change the path's `L130 100 L30 100` to `L80 100`, then click Reload: the apron is a hatched triangle in the large view and every small one.
32. Put `clear.svg` back and click Reload: the apron is a trapezoid again.
33. Click Sandbox, then Workbench: the level picker offers 8, 24 and 60 px/m with 60 picked, and the vise and toolbox stand on the worktop in every small view.
34. Change `carcass-front.svg`'s viewBox to `0 0 300 72` and click Reload: the summary turns red with `1 error`.
35. Read DRAWINGS: `carcass-front.svg` reads `off` in red with `5.6% off`.
36. Read VALIDATION: the proportion error is listed under `carcass`, and nothing else is listed.
37. Put `carcass-front.svg` back and click Reload: `0 errors`.
