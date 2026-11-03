# Scenario 08: HUD text fits

**Status:** ready  
**Last verified:** 2026-11-02

Every label in the game's HUD sits inside its box: buttons size to their labels, long names wrap.

---

## Steps

Launch `world-sim --scene=game` (RelWithDebInfo); the dev server is on 127.0.0.1:8081. Bob's position is in `/api/state?what=colonists`.

1. Look at the bottom bar: ACTIONS, BUILD, PRODUCTION, FURNITURE, ZONES and ROOMS each sit inside their own button, none touching the next.
2. Spawn a crafting spot beside Bob (`/api/dev/spawn?def=CraftingSpot&at=<x+5>,<y>&n=1&scatter=0`), select it, open its crafting menu and queue Basic Box and Basic Shelf.
3. Expand Tasks on the right: each row's description wraps inside the panel with its position and distance at the right end, nothing past the panel's edge.
4. Click Bob's card: in the panel, his task reads on one or more lines above a thin bar, and DRAFT, GO TO and PRIORITIES each fit their buttons.
5. Build > Wall: the material and thickness cards hold their names.
6. `/api/ui/lint`: no `text-overflow` or `child-outside-parent`.
