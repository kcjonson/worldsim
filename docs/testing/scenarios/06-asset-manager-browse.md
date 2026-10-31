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
6. Type part of a name in search: the tree narrows to matching assets.
7. Scroll the XML well and the tree: both scroll on their own, the XML sideways too, since its lines don't wrap.
8. Click Reload: the summary and tree rebuild, the selection kept when it still exists.
