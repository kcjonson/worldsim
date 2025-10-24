# Crafting and Resources

## Design Decisions

### Crafting Stations vs Tools
**Question:** Should there be designated crafting stations like "wood" and "stone" or just TOOLS such as "saw" and "drill"?

**Options:**
1. **Tool-based approach**: A collection of tools in a room would make a workshop. The ROOM could be responsible for creating items, not the world entity
2. **Station-based approach**: Specific crafting station that could be enhanced with other nearby tools, but crafting would still be done from the station

### Stone Naming
**Question:** Should stone use common names such as "granite" or scientific names such as "Igneous"?

**Options:**
1. **Scientific names**: Could come with a default name, but the player could rename it. Could come in a variety of colors such as "red sedimentary rock"
2. **Common names**: More familiar to players, easier to understand

## Resource Primitives

### Wood

**Discovery/Research Path:**
1. Discover tree (proximity)
2. Research tree (at tree)
3. Cut tree (if cuttable)
4. Use tree

**Crafting Stations:**
- Generic workbench
- Wood workbench
- Electric wood workbench
- Automated wood workbench

**Notes:**
- Should include brush and shrubs, other woody items
- These will not return craftable wood
- Should sticks be generic?

### Stone

**Research Path:**
1. Discover stone
2. Research stone
3. Harvest stone
4. Use stone

**Types:**
- **Igneous** - Volcanic origin
- **Sedimentary** - Layered deposits
- **Metamorphic** - Transformed rock

**Reference:** [DF2014:Stone - Dwarf Fortress Wiki](https://dwarffortresswiki.org/index.php/DF2014:Stone)

### Metals

*[To be defined]*

### Minerals

*[To be defined]*

### Foods

Lots of ways to classify this, needs more details.

**Classification options:**
- Edible / Inedible (Oxygen Not Included approach)
- Vegetarian / Meat (Rimworld approach)
- Ingredient / Meals (Cooking system approach)

## Related Systems

- See [skills.md](./skills.md) for how crafting relates to skill progression
- See [rooms.md](./rooms.md) for workshop types and bonuses
