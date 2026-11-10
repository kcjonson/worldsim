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
12. Add a comment line to `assets/shared/scripts/ground_grass.lua`, click Reload: the log says there's no bake of Ground_Grass under its current key, and it bakes again; the cache folder now holds two `Ground_Grass-` files. Take the comment out and click Reload: Ground_Grass's Bake shows at once, from the first file.
