# Scenario 09: ground tiles

**Status:** ready  
**Last verified:** 2026-11-02

The baked ground art, its disk cache, the height-contested edges between surfaces and the organic biome edges, in the game and in asset-manager. Spec: [ground-textures.md](../../technical/ground-textures.md).

---

## Steps

Build `world-sim` and `asset-manager` (RelWithDebInfo). The dev server is on 127.0.0.1:8081 (game) and 8070 (asset-manager). The bottom-left of the game reads `Position: (x, y)` and `Biome:`, so you can tell where a camera jump landed. A camera jump is `curl.exe "http://127.0.0.1:8081/api/control?action=camera&x=<x>&y=<y>&zoom=<z>"`; zoom 3 is the default (24 px/m), 20 is 160 px/m, 0.5 is 4 px/m. Wait a second or two after a jump into new chunks, and a few seconds for the far ones (steps 10 to 12).

1. Delete `%LOCALAPPDATA%\kcjonson\world-sim\cache\ground\`, the ground bake cache the game and asset-manager share, and launch `world-sim.exe` with no flags: the splash's last boot line counts `Baking ground tiles N/72` for about 21 s and ends on `Ground tiles baked`; the log has a `no bake of <def> under its current key` line for each def, and its `GroundTileBank:` line says `0 from the cache, 9 baked, 0 error(s)`. The cache folder now holds nine `<def>-<hash>-<key>.gtile` files, and nothing appears beside the exe under `build\apps\world-sim\RelWithDebInfo\cache\`.
2. Click Enter Expedition, then Quick Start: the game loads at the river landing, `Biome: TemperateDeciduousForest`.
3. `curl.exe "http://127.0.0.1:8081/api/control?action=exit"`, launch again, and wait on the splash: the boot line is done almost at once, and the log says `9 from the cache, 0 baked` with a wall clock under 100 ms. Click through to the game again.
4. `curl.exe "http://127.0.0.1:8081/api/control?action=camera&x=0&y=20&zoom=3"`: grass, a mud river bank, brown dirt patches; no faint lines on any 1 m tile edge.
5. Hold D for a second, then A: the ground scrolls with no line flicker at tile edges and no shimmer in the grass.
6. `curl.exe "http://127.0.0.1:8081/api/control?action=camera&x=30&y=41&zoom=20"`: grass with clover leaves, blades in several shades and dark soil peeks between the trees; look for a repeating 1 m or 4 m motif and find none. Zoom 8, then 3: still none.
7. Grass over Dirt, `...camera&x=15&y=41&zoom=20`: a dirt patch with pebbles in it, ringed by grass; the tufts break over its edge.
8. Mud at a river, `...camera&x=3&y=14&zoom=8`: the river bank is dark mud with pebbles, and the grass darkens toward it; the puddles stay inside the mud.
9. Inside the grass family, `...camera&x=-23&y=7&zoom=8`: darker tall-grass patches in the green; their edges are a soft change of sward, never a line or a dissolve.
10. Grass over Sand, `...camera&x=558&y=131484&zoom=8` (`Biome: SemiDesert`): the sand edge is ragged, with tufts over the sand.
11. Sand over Rock, `...camera&x=594&y=131495&zoom=20`: a grey outcrop in the sand; the sand reaches into its joints and hollows as ragged lobes, with no horizontal streaks.
12. Snow over Rock, `...camera&x=0&y=6000000&zoom=3` (`Biome: AlpineTundra`): white snow patches on grey stone; then `...camera&x=-34&y=5999994&zoom=20`: a crisp ragged edge with stone showing through the snow's rim, snow lying in the stone's low spots, no foggy halo.
13. Zoom pass, at each of steps 7 to 12: set zoom 20, 3, 1.5 and 0.5 in turn (`...camera&zoom=<z>`, or scroll). Each edge stays a soft ramp at every zoom, with no crisp line at 0.5.
14. At zoom 0.5 on the Grass over Sand view (`...camera&x=558&y=131484&zoom=0.5`), tap D and A a few times so the edge stays in frame: no shimmering line along it.
15. Biome edges, `...camera&x=540&y=131480&zoom=3`: the grass and sand boundary has bays, headlands and islands; no 16 m stair steps and no long straight runs.
16. Exit the game (`curl.exe "http://127.0.0.1:8081/api/control?action=exit"`), launch `asset-manager`: the tree has a Ground category listing all 9 surfaces (Ground_Dirt, Ground_Grass, Ground_GrassMeadow, Ground_GrassShort, Ground_GrassTall, Ground_Mud, Ground_Rock, Ground_Sand, Ground_Snow), each with a tile thumbnail.
17. Select Ground_Rock and pick Bake: it shows at once, from the bake the game stored in step 1, and the log's `Ground bake Ground_Rock` line says `from the cache`. The pane shows the mips row, the heights strip (black and white, one texel a pixel), the zoom strip and a metrics table; Next Variant steps through the 8.
