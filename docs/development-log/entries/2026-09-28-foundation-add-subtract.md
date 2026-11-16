# 2026-09-28 - Foundation Add / Subtract (construction G3)

## Summary

Foundations can be reshaped after drawing. The foundation panel's Add and Subtract buttons switch the drawing tool into an edit mode on that foundation: draw a polygon across its edge and close it. An editable blueprint (nothing delivered, no work, no demolish order) is unioned or carved in place and its manifest, work, and HP resize. A built foundation never shrinks; an Add onto it becomes an extension blueprint that colonists clear, supply, and build like any foundation, and that merges into the built one when it finishes. `/api/dev/foundation-edit` drives the same path for scripted checks.

## Details

Rules (from the plan, `.claude/plans/foundation-add-subtract.md`):

- Editable blueprint: Add unions in place, Subtract carves in place, both send the blueprint back to Clearing.
- Blueprint with material delivered or work done: both rejected ("materials already delivered").
- Built: Subtract rejected ("built foundations never shrink"); Add commits an extension (drawn minus target, target's material). One pending extension per target; a second Add waits ("finish the pending extension first").
- The resulting outline is judged, not the drawn polygon: closed shape constraints, point cap, min/max area, overlap and clearance against every other foundation. For a built Add the outline is the one the merge will produce. Subtract must also keep every hosted wall's full-thickness band inside the remainder.
- Boolean statuses map to reasons: disjoint, pinch, hole (cut or enclosed), split, consumes, nothing to add/remove. A built Add whose added region would wrap around the foundation or come out in two pieces is rejected too, since an extension is one simple polygon.
- Demolishing a target takes its pending extension with it (plain Demolish and Demolish building both mark it); the deconstruct cascade gate holds the target until the extension is gone, so an extension never outlives its target.

Engine:

- `ConstructionWorld`: `Foundation::mergeTarget`, `commitExtension`, `mergeExtension`, `pendingExtensionOf`. `commitExtension` refuses any ring whose union with the target fails, so the later merge cannot. The extension is exempt from the overlap check against its own target (shared edges, 1 mm boolean rounding). `removeFoundation` detaches an extension whose target vanished instead of leaving a dangling id.
- `ConstructionValidator`: `validateFoundationEdit` (one decision point returning the outline, for a built Add the extension ring, and for any Add the ground to check for buildability: the extension, or the drawn polygon on a blueprint) and `validateEditPoint` (the drawn polygon only has to be a usable cutter: point cap, vertex spacing, simple). `checkShape` split into its four checks; `checkOutline` is the closed-foundation gate shared with `validateRing`.
- New foundation-to-foundation clearance in `checkOutline`: any vertex of one ring closer than `pathingClearance` (0.7 m) to the other must itself touch it (within 2 mm, the snapped-edge case), so a foundation flush against one leg of an L cannot sit 0.3 m off the other. The design spec already required it; it applies to newly drawn foundations as well as edits.
- `StructureBlueprint::shapeEditable`; `ConstructionSystem` cascade gate waits on a pending extension.

App:

- `DrawingSystem`: `ToolKind::FoundationEdit`, `activateFoundationEditTool`, `applyFoundationEdit` (the one path for the tool and the dev verb), `mergeExtension`, `foundationEditable`. `spawnBlueprintEntity` reads material and ring from the record and shares one sizing helper with the in-place resize; the duplicate spawn in `DevCommandHandler` is gone. The preview draws the resulting outline (green) or the reason next to the cursor (red); the config strip shows the mode, the locked material, the resulting area, and its change.
- `GameScene`: structure-completed callback merges a finished extension and defers the extension entity's destroy; demolish handlers treat the extension as part of the foundation; panel callbacks activate the edit tool.
- `SelectionAdapter::adaptFoundation`: Add / Subtract buttons (omitted when they don't apply, like the Demolish swap), extension and pending-extension lines; callbacks bundled into `FoundationActions`.
- Review fixes: a foundation marked for demolition can't be edited ("being demolished"; the panel drops Add), since an extension made then would hold the teardown open without being part of it. A foundation entity's Position (the build, haul, and deconstruct goal destination) is `geometry::interiorPoint` of its ring, not the area centroid, which falls outside a U. An in-place resize drops the blueprint's goal tree (`ConstructionSystem::resetBlueprintGoals`) so the next tick rebuilds it at the new position and against the new manifest.
- Concave fills: merges and notches make concave foundations the normal case, and the interim render filled every ring with a fan from vertex 0, which paints wedges outside L and T outlines. `CommittedGeometryCache` now tessellates each foundation ring once per topology change with `renderer::Tessellator` (the rooms overlay's triangulator), and `DrawingSystem`'s screen-space fills (wall bands, junctions, openings, the in-progress foundation preview) go through the same tessellator. The fan code is gone.
- Debug server: `/api/dev/<verb>` now waits for the game thread to run the verb and answers with the handler's JSON (falls back to the old queued ack after 2 s when no game scene drains commands). That is what lets `foundation-edit` report status, reason, and ids.

Verified live (world-sim RelWithDebInfo, port 8082): a built 6x6 Wood foundation with an interior wall, a disjoint Add ("must touch the foundation"), a Subtract on it ("built foundations never shrink"), an Add across its east edge (extension #2, 24 m^2), a second Add ("finish the pending extension first"), freebuild merging it into one 60 m^2 foundation with a summed manifest and the wall untouched; a blueprint rejecting an interior Subtract ("can't cut a hole"), a notch Subtract shrinking 48 to 42 m^2 with the manifest resized 96 to 84, and an Add after a delivery ("materials already delivered"). The panel showed Add + Demolish building on the merged foundation, and clicking Add opened the edit strip.

## Related Documentation

- Design: `/docs/design/game-systems/world/building-construction.md` (Editing After Build: Add / Subtract; Info Panels)
- Architecture: `/docs/technical/building-construction-architecture.md` (section 4, G)
- Dev API: `/docs/testing/README.md`

## Next Steps

- Vertex editing on blueprints (Edit shape) remains deferred.
