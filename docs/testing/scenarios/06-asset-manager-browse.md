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

Preconditions: asset-manager running with World selected. `TestPartsAreas` (`assets/world/misc`) has two named use areas, `front` written as an XML rect and `side` drawn, and a clearance of a 0.1 m margin and a drawn apron; the Workbench is under Sandbox.

13. Search `TestParts` and pick TestPartsBox: the badge reads PARTS (not PROCEDURAL), the tree row shows the box front-down, and the pane shows the turn controls, the overlay toggles, a large view, and a sheet of 24 small views labelled 0 to 345.
14. Pick TestPartsAreas: green `front` and `side` areas lie under the box, each labelled, the clearance is hatched with a dashed edge, and the cyan collider outlines the box; every small view shows them turned with the box.
15. Click `+15` twice: the entry reads 30.00 and the large view and its overlays turn clockwise together.
16. Type `271` in the entry and press Enter: the view shows the left end and a sliver of the front.
17. Click the large view, then press Right: 286.00. Press Left: 271.00.
18. Click the small view labelled 90: the entry reads 90.00 and the large view matches it.
19. Untick Use areas, Clearance and Collider: the overlays go from the large view and every small one. Tick Footprints: each part's footprint is outlined. Tick them back.
20. The right column lists `clear.svg` and `side-use.svg`, each with what it maps onto, its size in metres, and `ok` with its proportions in green; under VALIDATION, `box` and `item` read `no issues`.
21. Narrow the apron in `clear.svg` (its path's last two points made one, `L80 100`) and click Reload: the hatched clearance before the box is a triangle in the large view and every small one. Put the file back and Reload.
22. Pick Sandbox, then Workbench: the vise and toolbox stand on the worktop at every turn of the sheet; the level picker offers Auto, 8, 24 and 60 px/m.
23. Change `carcass-front.svg`'s viewBox to `0 0 300 72` and Reload: the summary turns red with one error, `carcass-front.svg` reads `off` in red with `5.6% off`, and VALIDATION lists the proportion error under `carcass`. Put the file back and Reload: 0 errors.
24. `/api/ui/lint`: `"count":0`.
