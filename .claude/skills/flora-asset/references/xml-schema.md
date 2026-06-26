# AssetDef XML reference

Verified against `libs/engine/assets/AssetRegistry.cpp` and real assets. Re-read those if fields seem off.

## Folder + naming (load-bearing)

```
assets/world/flora/<Name>/<Name>.xml      <- primary def; filename MUST equal folder name
assets/world/flora/<Name>/<name>.svg      <- for simple assets, referenced by <svgPath>
```
- Discovery recursively scans `assets/` for `*.xml`; only the file whose name matches its parent folder is loaded as a primary def. Helper XMLs with other names are skipped.
- `<svgPath>` is relative to the asset folder. `<scriptPath>` uses `@shared/<file>.lua` for the shared scripts folder (`assets/shared/scripts/`).
- `misc/` holds rocks/props; `flora/` holds vegetation.

## Field reference

- `defName` — unique id, convention `Flora_<Thing>` (e.g. `Flora_TreeOak`).
- `label` — human name.
- `assetType` — `procedural` (Lua) or `simple` (SVG).
- `generator` (procedural) — `<scriptPath>` + `<params>` (each child is a named float/int/string read by `getFloat`/`getInt`/`getString`).
- `svgPath` (simple) — SVG filename.
- `worldHeight` (simple) — meters; SVG is scaled so its bounding-box height maps to this. Procedural geometry is already in meters, so procedural defs omit it.
- `rendering` — `<complexity>` `simple` | `complex`; `<tier>` `instanced` | `batched`; `<variantCount>` (also valid as a top-level element) number of pre-generated mesh variants.
- `animation` (optional) — `<type>parametric</type>`, `<windResponse>` 0..1, `<swayFrequency>min,max</swayFrequency>`.
- `collision` (optional) — physical footprint for nav + Tier-3 collision, separate from the render mesh. **Procedural assets emit it from the generator** (`asset:setCollisionRect(halfW, halfH, cx, cy)`; the shared tree scripts already emit a trunk rect from `trunkWidth`), so a procedural def usually needs no `<collision>` element. For a manual override (or a simple asset), declare `<collision><rect minX="" minY="" maxX="" maxY=""/></collision>` (a trunk-base footprint, not the canopy) or `<collision><polygon><point x="" y=""/>...</polygon></collision>` (>= 3 CCW points). XML wins over a generator emit. Rect or polygon only — there is no `<circle>`.
- `capabilities` (optional) — `<harvestable .../>`, attrs: `yield`, `amountMin`, `amountMax`, `duration`, `destructive`, `totalResourceMin`, `totalResourceMax`, `requiresTool`, `regrowthTime`.
- `placement` — one `<biome name="...">` per biome with `<spawnChance>` and a `<distribution>` (`uniform` | `clumped` | `spaced`) plus its params (`<clumping>` or `<spacing><minDistance>`). Plus `<groups>` and optional `<relationships>` (`<affinity .../>`, `<avoids .../>`).

Biome names must match the worldgen biome ids (e.g. `TemperateDeciduousForest`, `TemperateGrassland`, `TemperateWetland`, `SemiDesert`). Confirm against existing assets / worldgen before inventing one.

## Worked example — procedural tree (reuses a shared generator, no new code)

```xml
<?xml version="1.0" encoding="UTF-8"?>
<AssetDefinitions>
  <AssetDef>
    <defName>Flora_TreeBirch</defName>
    <label>Birch Tree</label>
    <assetType>procedural</assetType>

    <generator>
      <scriptPath>@shared/deciduous.lua</scriptPath>
      <params>
        <trunkHeight>1.6</trunkHeight>
        <trunkWidth>0.14</trunkWidth>
        <canopyRadius>0.6</canopyRadius>
        <branchCount>3</branchCount>
      </params>
    </generator>

    <rendering>
      <complexity>complex</complexity>
      <tier>batched</tier>
    </rendering>

    <!-- No <collision>: @shared/deciduous.lua emits the trunk rect from trunkWidth via asset:setCollisionRect. -->


    <capabilities>
      <harvestable yield="Wood" amountMin="5" amountMax="8" duration="7.0"
                   destructive="false" totalResourceMin="15" totalResourceMax="22" requiresTool="Axe"/>
    </capabilities>

    <placement>
      <biome name="TemperateDeciduousForest">
        <spawnChance>0.025</spawnChance>
        <distribution>spaced</distribution>
        <spacing><minDistance>5.0</minDistance></spacing>
      </biome>
      <groups>
        <group>trees</group>
        <group>deciduous_trees</group>
        <group>large_flora</group>
        <group>wild_plants</group>
        <group>wood_sources</group>
      </groups>
      <relationships>
        <avoids type="same" distance="5.0" penalty="0.15"/>
      </relationships>
    </placement>

    <variantCount>15</variantCount>
  </AssetDef>
</AssetDefinitions>
```

## Worked example — simple static SVG plant

```xml
<?xml version="1.0" encoding="UTF-8"?>
<AssetDefinitions>
  <AssetDef>
    <defName>Flora_Fernlet</defName>
    <label>Fernlet</label>
    <assetType>simple</assetType>
    <svgPath>fernlet.svg</svgPath>
    <worldHeight>0.3</worldHeight>

    <rendering>
      <complexity>simple</complexity>
      <tier>instanced</tier>
      <variantCount>1</variantCount>
    </rendering>

    <placement>
      <biome name="TemperateDeciduousForest">
        <spawnChance>0.02</spawnChance>
        <distribution>clumped</distribution>
        <clumping>
          <clumpSize>3,8</clumpSize>
          <clumpRadius>0.3,0.8</clumpRadius>
          <clumpSpacing>2,4</clumpSpacing>
        </clumping>
      </biome>
      <groups>
        <group>small_flora</group>
        <group>ground_cover</group>
      </groups>
    </placement>
  </AssetDef>
</AssetDefinitions>
```
