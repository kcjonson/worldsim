# Scenario 06: asset-manager browse

**Status:** draft  
**Last verified:** never

asset-manager on salal's ui: the tree, search, selection and the detail pane.

---

## Steps

Launch `asset-manager`; the dev server is on its usual port.

1. Look at the window: a top bar with search, Reload and a summary (`N assets - E errors, W warnings`), a tree of categories on the left.
2. `/api/ui/lint`: `"count":0`.
3. Click a category header: its rows collapse and the chevron points right; click again to expand.
4. Click an asset row: the row shows selected and the right pane fills with its kicker, name, badge, preview and XML.
5. Pick a procedural asset: six variant previews under Randomize; click Randomize and the previews change.
6. Type part of a name in search: the tree narrows to matching assets.
7. Scroll the XML well and the tree: both scroll on their own.
8. Click Reload: the summary and tree rebuild, the selection kept when it still exists.
