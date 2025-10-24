# Environmental Interactions - Game Design

Created: 2025-10-24
Status: Design

## Overview

The game world responds dynamically to player and NPC actions. Vegetation bends, objects move, and the environment provides visual and gameplay feedback.

## Trampling and Movement

### Grass Trampling

**Mechanic**:
- Entities create "trampling zones" as they move
- Grass within zone bends in direction of movement
- Bending proportional to entity size and speed
  - Small animal: Slight bending
  - Human colonist: Moderate bending
  - Large animal/vehicle: Significant bending

**Visual Feedback**:
- Trampled grass springs back over ~2-3 seconds
- Healthy grass recovers faster
- Unhealthy/dry grass stays bent longer
- Repeatedly trampled grass shows wear (color change, shorter height)

**Gameplay Effects**:
- **Trails Visible**: Player can see recent paths
- **Tracking**: Follow animal trails through grass
- **Pathfinding Hints**: AI prefers existing trails (less resistance)
- **Grass Damage**: High traffic areas become dirt paths over time

### Walking Through Vegetation

**Tall Grass**:
- Colonists move slower through dense grass (95% speed)
- Grass parts in front, closes behind
- Creates rustling sound

**Bushes/Shrubs**:
- Cannot walk through by default
- Can be cleared/cut
- Provides cover (stealth bonus)

**Trees**:
- Solid obstacle, cannot walk through
- Provides shade (temperature bonus)

## Harvesting and Gathering

### Grass Harvesting

**Player Action**: Colonist cuts grass for hay/fiber

**Visual**:
- Colonist swings scythe
- Grass in arc bends, then disappears
- Cut grass appears as resource item on ground
- Stubble remains (shorter grass)

**Regrowth**:
- Grass regrows over time (game-time days)
- Regrowth visible: short sprouts → full blades
- Regrowth rate depends on soil quality, season

### Berry Harvesting

**Player Action**: Colonist picks berries from bush

**Visual**:
- Colonist reaches toward bush
- Bush shakes/rustles
- Berries disappear from bush visual
- Bush appears less full

**Regrowth**:
- Berries regrow seasonally
- Bush returns to full appearance

### Tree Chopping

**Player Action**: Colonist chops tree with axe

**Visual Sequence**:
1. **Chopping**: Tree shakes with each strike
2. **Damage**: Notch appears on trunk (visual indicator)
3. **Tipping**: After sufficient damage, tree tips over
4. **Falling**: Tree falls in direction of last chop (animated)
5. **Cleanup**: Fallen tree can be processed into logs
6. **Stump**: Stump remains (can be removed later)

**Gameplay**:
- Falling trees can damage nearby objects
- Don't chop tree toward your base!
- Wood resource obtained

## Environmental Forces

### Wind Effects

**Visual**:
- Grass and trees sway in wind direction
- Stronger wind = more bending
- Gusts create wave patterns

**Gameplay**:
- Wind direction affects:
  - Fire spread (accelerated downwind)
  - Projectile trajectory (arrows, etc.)
  - Heat/cold dispersion
  - Smoke direction

### Rain Effects

**Visual**:
- Vegetation weighted down
- Grass droops slightly
- Leaves drip water

**Gameplay**:
- Grass grows faster after rain
- Trampled grass recovers slower when wet
- Mud forms in high-traffic areas

## Collision and Physics

### Soft Collisions (Grass, Small Plants)

**Behavior**:
- Entities push through, vegetation bends
- No movement speed penalty (or minimal)
- Purely visual feedback

**Implementation**:
- No physics collision
- Visual-only deformation based on entity proximity

### Hard Collisions (Trees, Rocks, Structures)

**Behavior**:
- Entities cannot pass through
- Pathfinding routes around
- Collision detection active

**Implementation**:
- Simplified collision shape (AABB or circle)
- Separate from visual geometry

## Player Feedback Systems

### Visual Feedback

**Entity Location**:
- Trampled grass shows where entities have been
- Disturbed vegetation marks activity

**Resource Status**:
- Tall grass = harvestable
- Short grass = recently harvested
- Bare ground = overharvested

**Environmental Conditions**:
- Wilted grass = drought
- Lush grass = plenty of water
- Discolored grass = poor soil / disease

### Audio Feedback

**Movement**:
- Footsteps change based on ground type
- Rustling when pushing through vegetation
- Different sounds for different vegetation types

**Harvesting**:
- Cutting grass: Swish sound
- Chopping tree: Axe strike, wood crack
- Picking berries: Rustling, pluck sound

**Environmental**:
- Wind: Volume/pitch varies with wind speed
- Rain: Patter on leaves
- Ambient: Birds, insects (affected by vegetation density)

## Performance Considerations

### Interaction Zones

**Radius**:
- Small entity: 1-2 tiles affected
- Medium entity: 2-4 tiles affected
- Large entity: 4-8 tiles affected

**Update Frequency**:
- Only update vegetation near moving entities
- Static entities don't update vegetation
- Limit updates to 100-200 grass blades per frame

### Spatial Partitioning

**Implementation**:
- Grid-based spatial hash
- Only check vegetation in entity's grid cell + neighbors
- Avoid checking entire world every frame

## Related Documentation

**Technical**:
- [/docs/technical/vector-graphics/animation-system.md](../../../technical/vector-graphics/animation-system.md) - Trampling implementation
- [/docs/technical/vector-graphics/collision-shapes.md](../../../technical/vector-graphics/collision-shapes.md) - Collision geometry

**Design**:
- [README.md](./README.md) - Vector asset creation
- [animated-vegetation.md](./animated-vegetation.md) - Vegetation animation behavior
