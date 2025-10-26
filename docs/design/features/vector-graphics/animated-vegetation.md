# Animated Vegetation - Game Design

Created: 2025-10-24
Status: Design

## Overview

Vegetation in world-sim (grass, trees, bushes) is dynamically animated, creating a living, breathing world that responds to wind and player actions.

## Grass Animation

### Visual Behavior

**Idle State** (no interaction):
- Gentle swaying in prevailing wind
- Each blade moves independently with slight time offset
- Natural wave patterns across grass fields
- Subtle variation in sway speed and amplitude

**Wind Gusts**:
- Occasional stronger gusts sweep across landscape
- Grass bends more dramatically in gust direction
- Wave propagates through field (not all grass at once)
- Returns to gentle sway after gust passes

**Trampling**:
- Grass bends flat when entity walks over it
- Gradual spring-back to upright position
- Slightly faster spring-back for healthy grass
- Wilted/damaged grass stays bent longer

**Harvesting/Eating**:
- Grass shrinks and fades when eaten by animals
- Blade bends toward eating entity
- Disappears when fully consumed
- Regrows over time (new blade animates upward)

### Gameplay Implications

**Visual Feedback**:
- Player can see wind direction and strength
- Trampled paths show recent entity movement
- Grazing areas visible by grass height/density

**Performance**:
- Grass density configurable (low/medium/high)
- Distant grass uses simpler animation or static
- Player can reduce grass count for performance

## Tree Animation

### Visual Behavior

**Trunk Sway**:
- Entire tree sways gently in wind
- Pivot point at base of trunk or pull point at top of spline?
- Stronger wind = more sway

**Branch Movement**:
- Branches flex independently
- Top of tree moves more than bottom
- Leaves rustle with branch movement

**Wind Response**:
- Light wind: Gentle sway, leaf flutter
- Medium wind: Noticeable bending, branch movement
- Strong wind: Dramatic bending, vigorous movement
- Calm: Tree settles to upright, minimal movement

**Chopping/Harvesting**:
- Tree shakes when struck with axe
- Multiple chops create progressive lean
- Final chop: Tree tips and falls (animated)
- Stump remains (static, no animation)

**Seasonal Changes** (future):
- Leaves change color (procedural color shift)
- Leaves fall off in autumn (particle-like)
- Snow accumulates on branches in winter

### Tree Types

**Small Trees** (bushes, saplings):
- Single-pivot sway
- Lighter, faster movement
- More responsive to wind

**Medium Trees**:
- Multi-point deformation (trunk segments)
- Moderate sway
- Visible branch movement

**Large Trees**:
- Complex multi-point animation
- Slower, heavier movement
- Top sways significantly, base stable

### Gameplay Implications

**Wind Indicator**:
- Trees show wind direction for navigation
- Useful for planning (fire spread, etc.)

**Resource State**:
- Healthy trees: Full animation
- Diseased trees: Wilted, less movement
- Dead trees: Static (no animation)

**Environmental Storytelling**:
- Forest density affects animation (crowded = less sway)
- Lone trees sway more freely
- Damaged trees lean, broken branches droop

## Bushes and Shrubs

### Visual Behavior

**Similar to small trees**:
- Whole-body sway
- Faster, livelier movement than trees
- Rustling effect

**Foraging Interaction**:
- Bushes shake when colonist harvests berries
- Branches spring back after harvest
- Visual depletion (fewer berries, less full appearance)

## Animation Parameters

### Wind Simulation

**Global Wind** (affects all vegetation):
- **Direction**: Changes gradually over time
- **Base Speed**: 0-10 (calm to storm)
- **Gustiness**: 0-1 (how often gusts occur)
- **Turbulence**: 0-1 (how chaotic)

**Per-Vegetation Properties**:
- **Stiffness**: How much it resists bending (0-1)
  - Grass: 0.2 (very flexible)
  - Young tree: 0.5
  - Old tree: 0.8 (rigid)
- **Mass**: Affects spring-back speed
- **Damping**: How quickly motion settles

### Animation Quality Settings

**Player Options**:

**Ultra** (performance cost: high):
- All vegetation animated
- Complex multi-point deformation for trees
- Per-blade grass animation

**High** (performance cost: medium):
- All visible vegetation animated
- Simplified tree animation (fewer deformation points)
- Grass groups animated together

**Medium** (performance cost: low):
- Nearby vegetation animated
- Distant vegetation static
- Grass in patches (not per-blade)

**Low** (performance cost: minimal):
- Only trees animated (simple sway)
- Grass static or very simple vertex shader wave

**Off**:
- All vegetation static

## Performance Budgets

**Target**: 10,000 animated grass blades @ 60 FPS

**LOD Strategy**:
- **0-10 units**: Full animation, individual blades
- **10-30 units**: Simplified animation, grouped blades
- **30-50 units**: Very simple animation (vertex shader)
- **50+ units**: Static

**Tree Budget**: 1,000 animated trees @ 60 FPS

## Audio Integration

**Wind Sounds**:
- Wind volume/pitch tied to wind speed
- Rustling leaves for trees
- Grass swishing for fields

**Interaction Sounds**:
- Footsteps different on grass vs bare ground
- Rustling when moving through tall grass
- Tree chopping sounds sync with shake animation

## Environmental Effects

**Rain**:
- Grass weighted down by water
- Leaves drip
- Slower spring-back when trampled

**Snow**:
- Grass buried (height reduced)
- Tree branches weighted (droop more)
- Snow falls off with wind gusts

**Fire** (future):
- Grass burns and crumbles
- Trees catch fire, branches burn away
- Charred remains static

## Related Documentation

**Technical**:
- [/docs/technical/vector-graphics/animation-system.md](../../../technical/vector-graphics/animation-system.md) - Animation implementation
- [/docs/technical/vector-graphics/lod-system.md](../../../technical/vector-graphics/lod-system.md) - Level of detail

**Design**:
- [README.md](./README.md) - Asset creation guidelines
- [environmental-interactions.md](./environmental-interactions.md) - Player interaction mechanics
