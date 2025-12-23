# Main Game UI Design

**Status:** Design (Initial Draft)  
**Created:** 2025-12-14  
**Last Updated:** 2025-12-14

## Overview

This document defines the user interface for worldsim's main gameplay view—the screen where players manage their colony during active gameplay. It draws from established patterns in colony simulation games (RimWorld, Oxygen Not Included, Dwarf Fortress) while accommodating worldsim's unique technical characteristics: pure vector graphics, continuous pathfinding, and autonomous colonist behavior with memory-based decision making.

### Design Philosophy

Colony simulation UIs face a fundamental tension: players need comprehensive information and control, but the interface shouldn't feel like "playing the UI" instead of engaging with the world. Our approach:

1. **At-a-glance information** — Critical status (colonist health, mood, current activity) visible without clicking
2. **Contextual depth** — Details appear when relevant, not constantly present
3. **Minimal screen obstruction** — Maximize viewport of the game world
4. **Consistent interaction model** — Select → Inspect → Act pattern throughout
5. **Autonomous colonists** — UI reflects that colonists self-manage; player intervention is for exceptions, not micromanagement

### Screen Layout Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│ [Date/Time] [Speed] [Menu]                                          │  ← Top Bar (Left)
├────────────────────────────────────────────────────────────────────┤
│ ┌────────┐                                         ┌──────────────┐ │
│ │Colonist│                                         │   Minimap    │ │
│ │  List  │                                         │  [+][-][⟳]   │ │
│ │ (Top   │                                         └──────────────┘ │
│ │  Left) │                                         [📦 Resources ▼] │  ← Link to expand
│ │        │         GAME VIEWPORT                                    │
│ │        │                                                          │
│ │        │            [+]  [-]                                      │  ← Zoom controls
│ │        │                                                          │
│ └────────┘                                                          │
│ ┌────────────────────┐                                              │
│ │ Debug Info         │                                              │
│ │ Chunk: (3,2)       │                                              │
│ │ Pos: 142.3, 87.1   │                                              │
│ └────────────────────┘                                              │
│ ┌────────────────────────────────────────────────────┐              │
│ │ Inspect Panel (appears when entity selected)       │ ┌──────────┐ │
│ │ Shows colonist/building/item details               │ │ Notifs   │ │
│ └────────────────────────────────────────────────────┘ │ (toasts) │ │
│                                                        └──────────┘ │
├────────────────────────────────────────────────────────────────────┤
│ [Actions▾] [Build▾] [Production] [Furniture]                        │  ← Gameplay Bar
└─────────────────────────────────────────────────────────────────────┘
```

---

## 1. Top Bar (Always Visible)

A persistent thin bar on the **left side** providing game state and controls.

### 1.1 Date/Time Display (Left)

```
Day 15, Summer | 14:32
```

- **Day number** — Days since colony founding
- **Season** — Current season (affects weather, crop growth)
- **Time of day** — 24-hour clock
- Clicking opens detailed calendar view (future: events, scheduled tasks)

### 1.2 Game Speed Controls (Center)

```
[⏸] [▶] [▶▶] [▶▶▶]
```

- **Pause** (Space) — Game frozen, commands still valid
- **Normal** (1) — 1x speed
- **Fast** (2) — 3x speed
- **Very Fast** (3) — 10x speed

**Visual Feedback:** Current speed highlighted; world rendering reflects speed (e.g., faster colonist animation at higher speeds)

**Hotkeys:** 
- `Space` — Toggle pause
- `1/2/3` — Set speed
- `+/-` — Increment/decrement speed

### 1.3 Menu Button

Opens pause menu: Save, Load, Settings, Quit to Main Menu

---

## 2. Colonist List (Top-Left, Vertical)

A vertical list of colonist portraits extending down from the top-left corner. Compact design—no expanded state.

### 2.1 Portrait Design

Each colonist represented as a minimal card:

```
┌─────────────┐
│ [Portrait]  │  ← Face/avatar (background tinted by status)
│ ████████░░  │  ← Mood bar
│ Sarah       │  ← Name
└─────────────┘
```

**Portrait:** Small avatar showing colonist appearance. Background color indicates overall status:
- Green tint: Good mood, healthy
- Yellow tint: Stressed or minor issues
- Red tint: Critical (mental break imminent, injured, starving)
- Grayed: Incapacitated or sleeping

**Mood Bar:** Horizontal bar showing mood percentage. Color gradient follows mood level.

### 2.2 Interactions

- **Click** — Select colonist → Shows full details in **Inspect Panel** (bottom-left)
- **Double-click** — Zoom camera to colonist's location on the map
- **Right-click** — Context menu (Prioritize, View Details)

### 2.3 Scrolling

- If more colonists than fit on screen, the list becomes scrollable
- No expanded/collapsed states—always compact
- Hovering shows tooltip with current activity

**Scaling:** With many colonists, scroll to access. Each portrait stays compact to maximize list capacity.

---

## 3. Minimap (Top-Right Corner)

A small overhead view of the explored map showing terrain, buildings, and entity positions.

### 3.1 Minimap Controls

```
┌──────────────────────┐
│                      │
│      [Minimap]       │
│                      │
│  [+] [-] [⟳]         │  ← Zoom In, Zoom Out, Reset
└──────────────────────┘
```

- **[+]** — Zoom in on minimap (show less area, more detail)
- **[-]** — Zoom out on minimap (show more area, less detail)
- **[⟳]** — Reset to default zoom level

### 3.2 Display Content

- **Terrain** — Base ground colors (biomes, water)
- **Buildings** — Colony structures highlighted
- **Colonists** — Small dots for colonists (colored by status)
- **Threats** — Raiders, predators highlighted in red

**Future (Deprioritized):** Toggleable display layers for different entity types.

### 3.3 Interactions

- **Click** — Jump camera to location
- **Drag rectangle** — Set camera viewport
- **Mouse wheel** — Zoom minimap itself (same as +/- buttons)

### 3.4 Visual Design

- Semi-transparent background so it doesn't fully obscure game world
- White rectangle indicates current camera viewport
- Pulsing indicators for alerts (attack, colonist in danger)

---

## 4. Resources Panel (Top-Right, Below Minimap)

**Not persistently displayed.** A small link/button below the minimap opens an expandable panel.

### 4.1 Collapsed State (Default)

```
[📦 Storage ▼]
```

A small clickable link that expands the resources panel downward.

### 4.2 Expanded State

```
┌─────────────────────────┐
│ 📦 Storage           [▲]│
├─────────────────────────┤
│ ▶ Food                  │
│   ▶ Prepared Meals (12) │
│   ▶ Raw Meat (24)       │
│   ▶ Vegetables (45)     │
│     • Potatoes (20)     │
│     • Carrots (15)      │
│     • Corn (10)         │
│ ▶ Materials             │
│   ▶ Wood (142)          │
│   ▶ Stone (89)          │
│   ▶ Metal (34)       ★  │  ← Pinned (always show)
│ ▶ Components            │
│   ...                   │
└─────────────────────────┘
```

### 4.3 Key Features

**Nested Tree View:**
- Categories expand/collapse with `▶` / `▼` arrows
- All categories collapsed by default
- Sub-categories can nest further

**Storage Containers Only:**
- This displays the **sum of items in storage containers** (stockpiles, shelves, warehouses)
- Does **NOT** include items lying on the ground, in colonist inventories, or resources in the world (unmined ore, growing plants, etc.)
- This is "what we have available to use"

**Pin to Always Show:**
- Click the star `★` next to any resource to "pin" it
- Pinned resources appear in a compact display even when panel is collapsed:
  ```
  [📦 Storage ▼] Metal: 34
  ```
- Allows players to track specific resources they care about

### 4.4 Interactions

- **Click category arrow** — Expand/collapse that category
- **Click resource** — Future: highlight where this resource is stored
- **Click star** — Toggle "always show" for this resource
- **Click header [▲]** — Collapse entire panel

### 4.5 Space Constraints

- Resources panel expands downward as much as available space allows
- **Must reserve space** for at least 2 notifications at the bottom
- If many resources are expanded, the panel becomes scrollable rather than pushing notifications off-screen

---

## 5. Zoom Controls (Main Viewport)

Floating zoom controls for the main game viewport.

### 5.1 Placement

Positioned in the game viewport area (not overlaying UI panels). Suggested: center-right or bottom-center of viewport.

```
    [+]
    [⟳]
    [−]
```

Or horizontal: `[−] [⟳] [+]`

### 5.2 Functionality

- **[+]** — Zoom in (closer to ground, more detail)
- **[−]** — Zoom out (farther from ground, see more area)
- **[⟳]** — Reset to default zoom level (clickable)
- **Mouse Wheel** — Also controls zoom (primary method)
- **Home key** — Reset to default zoom (keyboard shortcut)

### 5.3 Zoom Range

- **Close:** See individual items, colonist details, construction progress
- **Medium:** Standard gameplay view, see room-sized areas
- **Far:** Strategic overview, see large portions of map

---

## 6. Notifications (Bottom-Right, Toast Style)

Event notifications appear as toast-style popups rising from the bottom-right corner.

### 6.1 Toast Design

```
┌────────────────────────────────────────┐
│ Research Complete                  🔬  │
│ "Stonecutting" has been researched.    │
│                               [5s] [X] │
└────────────────────────────────────────┘
```

**Layout:**
- **Title** (bold) on top-left
- **Icon** on the right (not just an icon—includes descriptive text)
- **Description** below title
- **Timer** (for auto-dismiss) or **[X]** (for persistent)

### 6.2 Notification Types

**Auto-Dismiss (Timed):**
- Research complete
- Construction finished
- Trade caravan arrived
- Colonist completed task
- Default: 5-10 seconds, then fade out

**Persistent (Requires Acknowledgment):**
- Raid incoming
- Colonist downed
- Fire spreading
- Starvation imminent
- Mental break in progress
- Must click [X] or click notification to dismiss

### 6.3 Stacking

- Multiple notifications stack vertically (newest at bottom)
- Notifications extend upward into available space, **up to the minimap**
- If many notifications, they appear **above** the Resources panel
- The Resources panel reserves space for at least 2 notifications at minimum (does not expand to block all notification space)
- Click any notification to jump to relevant location/entity

### 6.4 Visual Styling

**By Severity:**
- **Critical** (Red background): Immediate threats, colonist danger
- **Warning** (Yellow/Orange background): Needs attention soon
- **Info** (Blue/Gray background): Informational updates

**Animation:**
- Slide in from right
- Fade out when dismissed or timed out
- Subtle pulse for critical notifications

---

## 7. Inspect Panel (Bottom-Left)

The generic inspection area. Shows brief details for whatever the player has selected. Appears **above** the debug info text currently shown in the bottom-left.

### 7.1 Panel Behavior

- **Hidden** when nothing selected
- **Appears** when player selects any entity (colonist, production entity, item, world entity)
- **Positioned** bottom-left, above the debug/game info text
- **Dismissible** via Escape key or clicking empty space
- **Brief** — detailed information lives in modal dialogs, not here

### 7.2 Colonist Selected

```
┌────────────────────────────────────────────────────────────────────┐
│ [Portrait]  Sarah Chen, 28                              [📋 Details]│
│             Mood: 65% (Content)                                     │
│                                                                     │
│ Current: Hauling wood               Needs:                          │
│ Next: Eat meal                      ████████░░ Hunger (72%)        │
│                                     ██████████ Energy (100%)        │
│ Gear:                               ██████░░░░ Recreation (45%)    │
│  • Cloth shirt                                                      │
│  • Leather pants                                                    │
│  • Wooden spear                                                     │
└────────────────────────────────────────────────────────────────────┘
```

**Key Elements:**
- Portrait with name and age
- Current mood (brief)
- Current and next activity
- Needs bars (compact)
- Gear list (what they're wearing/carrying)
- **[📋 Details]** button — Opens full Colonist Details Modal (see Section 8)

**Note:** Clicking the Details button opens the modal AND closes/deselects the inspect panel.

### 7.3 Production Entity Selected

Production entities are workstations where colonists craft items (benches, stoves, research tables).

```
┌────────────────────────────────────────────────────────────────────┐
│ [Icon]  Crafting Bench                                 [X] Close   │
│         Status: Idle                                               │
│         Assigned: Sarah Chen                                       │
│                                                                    │
│ Bills:                                              [Add Bill]     │
│ ┌──────────────────────────────────────────────────┐              │
│ │ 1. Make wooden club (0/3)          [▲][▼][X]     │              │
│ │ 2. Make wooden door (forever)      [▲][▼][X]     │              │
│ └──────────────────────────────────────────────────┘              │
│                                                                    │
│ [Deconstruct] [Rename]                                            │
└────────────────────────────────────────────────────────────────────┘
```

**Production Queue ("Bills"):**
- Add items to craft
- Set quantity (X, until X, forever)
- Reorder priority
- Pause/resume individual bills

### 7.4 Item Selected

Items are objects that can be picked up, stored, or used.

```
┌────────────────────────────────────────────────────────────────────┐
│ [Icon]  Steel x42                                                  │
│         Quality: —                                                 │
│         Weight: 21kg                                               │
│                                                                    │
│ [Haul] [Deny]                                                      │
└────────────────────────────────────────────────────────────────────┘
```

### 7.5 World Entity Selected

World entities are natural objects in the world: trees, bushes, rocks, animals.

**Flora (Trees, Bushes, Plants):**
```
┌────────────────────────────────────────────────────────────────────┐
│ [Icon]  Oak Tree                                                   │
│         Growth: Mature                                             │
│         Health: 100%                                               │
│         Yields: ~40 wood                                           │
│                                                                    │
│ [Deny]                                                             │
└────────────────────────────────────────────────────────────────────┘
```

**Fauna (Animals):**
```
┌────────────────────────────────────────────────────────────────────┐
│ [Icon]  Deer (Female)                                              │
│         Age: Adult                                                 │
│         Health: 100%                                               │
│         Behavior: Grazing                                          │
│                                                                    │
│ [Deny]                                                             │
└────────────────────────────────────────────────────────────────────┘
```

**Note:** `[Deny]` prevents colonists from auto-harvesting/hunting this specific entity.

### 7.6 Terrain (Not Selectable)

Terrain tiles (grass, dirt, water, rock) are **not selectable**. 

**Future consideration:** Mouse-over tooltip showing terrain info without using the inspect panel. This would be a lightweight overlay that appears on hover, not a selection state.

---

## 8. Colonist Details Modal

A full-screen modal dialog showing comprehensive information about a colonist. Opened via the [📋 Details] button in the Inspect Panel.

### 8.1 Modal Behavior

- **Opens** when clicking [📋 Details] on colonist inspect panel
- **Closes** the inspect panel and deselects the colonist
- **Full screen overlay** with semi-transparent background
- **Dismissible** via [X] button, Escape key, or clicking outside modal
- **Pauses game** while open (optional, configurable)

### 8.2 Modal Layout

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        Sarah Chen                                   [X] │
│ ═══════════════════════════════════════════════════════════════════════ │
│                                                                         │
│  [Bio]  [Health]  [Social]  [Gear]  [Memory]                           │
│ ─────────────────────────────────────────────────────────────────────── │
│                                                                         │
│  ┌─────────────┐                          │ Current Mood: 65% (Content) │
│  │             │   Age: 28                │ ████████████████░░░░░░░░░░  │
│  │  [Portrait] │   Background: Farmer     │                             │
│  │             │                          │ Mood Factors:               │
│  └─────────────┘   Traits:                │  +15  Ate fine meal         │
│                    • Hard Worker          │  +10  Comfortable bed       │
│                    • Optimist             │  -5   Witnessed death       │
│                    • Night Owl            │  -3   Ugly environment      │
│                                           │                             │
│  Backstory:                               │                             │
│  Sarah grew up on a farming colony in     │                             │
│  the outer rim. She learned to work the   │                             │
│  land from an early age...                │                             │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

**Layout:** Two-column design with biography/traits on left, mood status on right.

### 8.3 Tab Contents

**Bio Tab:**
- Full portrait
- Age, background
- Personality traits with descriptions
- Backstory text
- Current mood with contributing factors

**Health Tab:**
- Body diagram showing injuries/conditions
- List of current ailments
- Treatment status
- Medical history

**Social Tab:**
- Relationships with other colonists
- Opinion modifiers (+/- reasons)
- Recent social interactions
- Family connections (if any)

**Gear Tab:**
- Currently equipped items (detailed)
- Inventory contents
- Equipment quality and condition

**Memory Tab (worldsim-specific):**
- Full list of known entities
- Categorized: Food sources, Water, Resources, Threats, Colonists, Locations
- Each entry shows: when discovered, how learned (observed/told by whom)
- Search/filter functionality

### 8.4 Memory Tab Detail

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Memory                                                    [Search: __] │
│ ─────────────────────────────────────────────────────────────────────── │
│                                                                         │
│  Known Entities: 47 total                                               │
│                                                                         │
│  ▼ Food Sources (12)                                                    │
│    • Berry bush at (142, 87)         Day 12 — Observed                 │
│    • Apple tree at (156, 92)         Day 13 — Observed                 │
│    • Potato field at (80, 45)        Day 14 — Marcus told me           │
│    ...                                                                  │
│                                                                         │
│  ▶ Water Sources (3)                                                    │
│  ▶ Resources (30)                                                       │
│  ▶ Threats (2)                                                          │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 9. Gameplay Bar (Bottom)

The primary action interface. This is where players perform most gameplay actions. Renamed from "Architect Bar" to reflect that this covers all gameplay actions, not just building.

### 9.1 Category Structure

```
[Actions▾] [Build▾] [Production] [Furniture]
```

Each category button expands a horizontal sub-menu above it showing available options.

### 9.2 Category Breakdown

#### Actions
Player interventions for when autonomous colonist behavior needs override:

- **Haul (Force)** — Force colonists to haul specific items to storage now (colonists normally haul as needed)
- **Deconstruct** — Mark built structures for removal
- **Deny** — Prevent colonists from auto-harvesting/interacting with specific items (e.g., "don't eat this food, it's for trade")
- **Cancel** — Remove pending designations

**Design Note:** Colonists are highly autonomous in worldsim. They will cut trees, gather food, harvest crops, and collect resources as they need them. The Actions menu is for **exceptions**—when the player wants to override autonomous behavior.

#### Build (Structures)
Building construction. See [Construction System Spec] for details on how placement works.

- **Walls & Doors** — Walls, doors, fences
- **Floors** — Flooring types

**Note:** We do not have roofs (outdoor game). Bridges, power systems, and security structures are future features.

#### Production
Workstations and production facilities:

- **Crafting** — Workbenches, craft spots
- **Cooking** — Stoves, butcher tables
- **Research** — Research benches
- **Processing** — Smelters, refineries (future)

#### Furniture
Interior and functional items:

- **Beds** — Sleeping furniture
- **Tables & Chairs** — Dining, working
- **Storage** — Shelves, containers
- **Recreation** — Entertainment items
- **Decor** — Aesthetic items

### 9.3 Build Mode Flow

1. Player clicks category (e.g., "Furniture")
2. Sub-menu appears showing items in category
3. Player clicks item (e.g., "Bed")
4. Cursor changes to placement mode
5. Player clicks/drags to place blueprint(s)
6. `R` to rotate, `Escape` to cancel
7. Blueprints added to construction queue

**Ghost Preview:** Semi-transparent preview of item at cursor position. Red tint if placement invalid (blocked, wrong terrain).

### 9.4 Hotkeys

Every gameplay bar action should have a hotkey:
- `A` — Open Actions menu
- `B` — Open Build menu
- `Q` — Quick-access last used item
- `R` — Rotate placement
- `Escape` — Cancel current action

---

## 10. Debug/Game Info (Bottom-Left, Below Inspect)

Existing debug information display. Stays in its current position.

```
┌────────────────────┐
│ Chunk: (3, 2)      │
│ Pos: 142.3, 87.1   │
│ Entities: 1,247    │
│ FPS: 60            │
└────────────────────┘
```

This appears below the Inspect Panel when one is open, or alone in the bottom-left when nothing is selected.

---

## 11. Management Screens (Full-Screen Overlays)

Accessed via hotkeys or menu buttons. These replace the game view temporarily.

### 11.1 Work Priorities (`W`)

Grid showing all colonists vs. all work types:

```
           Mining  Hauling  Cooking  Crafting  Research  ...
Sarah        3        4        —         1         2
Marcus       1        2        4         —         —
Elena        —        3        1         2         —
```

- Numbers 1-4 indicate priority (1 = highest)
- `—` means disabled for that colonist
- Click to cycle priority, right-click to disable
- Toggle between simple (on/off) and priority (1-4) modes

### 11.2 Schedule (`S`)

Hour-by-hour schedule for each colonist:

```
           0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 ...
Sarah     [Sleep--------] [Work----------------] [Rec] [Work...
Marcus    [Sleep--------] [Work----------------] [Rec] [Work...
```

- Color-coded blocks: Sleep, Work, Recreation, Anything
- Drag to paint time blocks
- Copy schedules between colonists

### 11.3 Research (`R`)

Tech tree visualization showing available research:

```
[Agriculture] ──→ [Irrigation] ──→ [Hydroponics]
      │
      └──→ [Brewing]

[Stonecutting] ──→ [Masonry] ──→ [Architecture]
```

- Click to select research target
- Shows requirements, costs, unlock benefits
- Progress bar for current research

### 11.4 History/Log (`H`)

Log of important events:
```
Day 15, 14:32 — Sarah completed wooden club
Day 15, 11:05 — Raid incoming from the east!
Day 14, 22:15 — Marcus had a mental break (minor)
Day 14, 08:00 — Harvest complete: 45 potatoes
```

---

## 12. Camera Controls

### 12.1 Movement

- **WASD / Arrow Keys** — Pan camera
- **Middle Mouse Drag** — Pan camera
- **Edge Scroll** (optional, in settings) — Move camera when cursor at screen edge

### 12.2 Zoom

- **Mouse Wheel** — Zoom in/out
- **[+]/[-] buttons** — Zoom in/out (on-screen controls)
- **Home** — Reset to default zoom

### 12.3 Focus

- **Double-click colonist in list** — Jump camera to colonist
- **Click notification** — Jump to relevant entity/location
- **Click minimap** — Jump to that location

### 12.4 Hotkeys (Location Bookmarks)

- **Shift+1-9** — Set location bookmark
- **1-9** — Jump to bookmarked location
- **Backspace** — Return to last camera position

---

## 13. World-Space UI Elements

UI elements rendered in the game world rather than screen-space overlays.

### 13.1 Selection Indicators

- **Selected entity:** White outline/highlight
- **Hover:** Subtle glow
- **Group selection:** Box outline around group

### 13.2 Colonist Labels (Toggle)

```
     Sarah
   [████░░] ← Mood bar
```

- Appears above colonist head
- Toggleable via `L` key
- Shows name and mood bar; hover for more details

### 13.3 Blueprint/Construction Preview

- Semi-transparent ghost of building being placed
- Progress bars on structures under construction
- Red overlay for invalid placement

### 13.4 Threat Indicators

- Red pulsing circles around hostile entities
- Direction arrows at screen edge when threats off-screen

---

## 14. Tooltips

Consistent tooltip system throughout:

- **Delay:** 0.5s hover before showing
- **Position:** Above cursor, adjusting to stay on-screen
- **Content:** Name, description, stats, hotkey hint

Example tooltip for colonist in list:
```
┌─────────────────────────────┐
│ Sarah Chen                  │
│ Mood: 65% (Content)         │
│ Current: Hauling wood       │
│                             │
│ Click to inspect            │
│ Double-click to find        │
└─────────────────────────────┘
```

---

## 15. Performance Considerations

### 15.1 UI Update Frequency

Not everything needs 60fps updates:

| Element | Update Rate | Notes |
|---------|-------------|-------|
| Game viewport | 60fps | Rendering |
| Colonist list portraits | 10fps | Smooth enough for mood bar animation |
| Resource counts | 1/tick | On game tick only |
| Minimap | 5fps | Sufficient for tracking |
| Inspect panel | On change | Event-driven |
| Notifications | On event | Event-driven |
| Tooltips | On hover | Event-driven |

### 15.2 Culling

- Off-screen colonist list items don't render
- Minimap entities culled by zoom level

### 15.3 Batching

- All UI text batched into single draw call per font size
- Icons batched into texture atlases
- Minimize state changes between UI elements

---

## 16. worldsim-Specific Features

Elements unique to our game that differ from typical colony sims.

### 16.1 Autonomous Colonist Design

Unlike RimWorld where players designate every tree to cut and every item to haul, worldsim colonists are highly autonomous:

- Colonists gather resources when **they** need them
- Players intervene only for exceptions (force haul, deny access)
- The Actions menu is intentionally minimal—colonists self-manage

This affects UI by:
- Fewer designation tools needed
- Focus on information display over micromanagement
- Memory system UI shows what colonists know (and thus can interact with)

### 16.2 Memory Visualization Mode

Special overlay showing what selected colonist knows:
- Bright: Entities this colonist knows about
- Dim: Entities they don't know
- Useful for debugging "why won't Bob go eat?"

Toggle via `M` key when colonist selected.

### 16.3 Continuous Position Indicators

Since colonists aren't tile-locked:
- Precise position shown in debug info and inspect panel
- Movement paths visualized with smooth curves, not tile-to-tile jumps
- Facing direction visible (colonists rotate freely)

### 16.4 Social Knowledge Visualization

When inspecting colonist memory, show:
- Who told them about each known entity
- When they learned it
- Knowledge source (observed directly vs. learned from others)

```
Berry bush at (142, 87)
  Discovered: Day 12
  Source: Observed directly
  
Iron deposit at (200, 340)
  Discovered: Day 14
  Source: Elena mentioned it
```

---

## 17. UI Primitives Required

Before implementing the gameplay UI, we need a component library. This section identifies the primitive UI elements required to build the screens described in this document.

### 17.1 Core Primitives

These are the fundamental building blocks:

| Primitive | Description | Used In |
|-----------|-------------|---------|
| **Text** | Rendered text with font, size, color, alignment | Everywhere |
| **Icon** | Small image/glyph rendering | Buttons, notifications, colonist status |
| **Image** | Larger image rendering (portraits, previews) | Colonist portraits, item icons |
| **Rectangle** | Filled/stroked rectangle with optional rounded corners | Panels, buttons, progress bars |
| **Progress Bar** | Horizontal bar showing percentage | Mood bars, needs bars, construction progress |

### 17.2 Interactive Components

| Component | Description | Used In |
|-----------|-------------|---------|
| **Button** | Clickable element with label/icon, hover/pressed states | Gameplay bar, panel actions, modal controls |
| **Icon Button** | Button with icon only (no label) | Zoom controls `[+][-][⟳]`, close `[X]` |
| **Toggle Button** | Button with on/off state | Speed controls, overlay toggles |
| **Dropdown Button** | Button that expands a menu below/above | Gameplay bar categories `[Actions▾]` |

### 17.3 Container Components

| Component | Description | Used In |
|-----------|-------------|---------|
| **Panel** | Rectangular container with optional border/background | Inspect panel, resources panel, debug info |
| **Modal Dialog** | Full-screen overlay with centered content, blocks input behind | Colonist Details Modal, management screens |
| **Scrollable Container** | Container with vertical scrollbar when content overflows | Colonist list, resources tree, bill lists |
| **Tab Container** | Container with tab buttons that switch visible content | Colonist modal tabs (Bio/Health/Social/Gear/Memory) |

### 17.4 Layout Components

| Component | Description | Used In |
|-----------|-------------|---------|
| **Horizontal Stack** | Arranges children horizontally with spacing | Top bar elements, button groups |
| **Vertical Stack** | Arranges children vertically with spacing | Colonist list, notification stack |
| **Two-Column Layout** | Side-by-side columns with configurable widths | Modal bio (left) + mood (right) |
| **Grid** | Rows and columns layout | Work priorities matrix |

### 17.5 Data Display Components

| Component | Description | Used In |
|-----------|-------------|---------|
| **Tree View** | Expandable/collapsible hierarchical list | Resources panel |
| **Accordion** | Collapsible sections (like tree but for content blocks) | Memory tab categories |
| **List Item** | Styled row for lists (icon + text + actions) | Bill queue items, gear list |
| **Labeled Value** | Label: Value pair display | "Age: 28", "Status: Idle" |
| **Badge** | Small colored tag with text | Trait badges, status indicators |

### 17.6 Notification Components

| Component | Description | Used In |
|-----------|-------------|---------|
| **Toast** | Temporary notification popup with icon, title, description | Notification system |
| **Toast Stack** | Container managing toast positioning and animation | Bottom-right notification area |

### 17.7 Specialized Components

| Component | Description | Used In |
|-----------|-------------|---------|
| **Minimap** | Rendered overview of game world with viewport indicator | Top-right minimap |
| **Portrait Card** | Colonist portrait with mood bar and name | Colonist list |
| **Tooltip** | Popup on hover with additional information | Throughout UI |
| **Context Menu** | Right-click popup menu | Colonist right-click, entity right-click |
| **Hotkey Hint** | Small text showing keyboard shortcut | Buttons, menu items |

### 17.8 Primitive Dependency Graph

Build order based on dependencies:

```
Level 0 (No dependencies):
  Text, Rectangle, Icon, Image

Level 1 (Depends on Level 0):
  Progress Bar (Rectangle + Text)
  Button (Rectangle + Text + Icon)
  Icon Button (Rectangle + Icon)
  Panel (Rectangle)

Level 2 (Depends on Level 1):
  Toggle Button (Button + state)
  Dropdown Button (Button + Panel)
  Scrollable Container (Panel + scroll logic)
  List Item (Panel + Icon + Text + Button)
  Labeled Value (Text + Text)

Level 3 (Depends on Level 2):
  Tab Container (Panel + Button row + content switching)
  Tree View (Scrollable Container + List Items + expand/collapse)
  Accordion (Panel + collapsible sections)
  Toast (Panel + Icon + Text + timer/dismiss)
  Portrait Card (Panel + Image + Progress Bar + Text)
  Tooltip (Panel + Text + positioning logic)

Level 4 (Depends on Level 3):
  Modal Dialog (Panel + overlay + Tab Container)
  Toast Stack (container + Toast instances + animation)
  Context Menu (Panel + List Items + positioning)

Level 5 (Specialized):
  Minimap (custom rendering + viewport indicator)
```

### 17.9 State Management Needs

Components need to handle:

- **Hover state** — Visual feedback on mouse over
- **Pressed state** — Visual feedback on mouse down
- **Disabled state** — Grayed out, non-interactive
- **Focus state** — Keyboard navigation indicator
- **Selected state** — For toggles, list items, tabs

### 17.10 Animation Requirements

| Animation | Components | Description |
|-----------|------------|-------------|
| Fade in/out | Toast, Tooltip, Modal | Opacity transition |
| Slide | Toast (from right), Dropdown (expand down) | Position transition |
| Expand/Collapse | Tree View, Accordion | Height transition |
| Progress | Progress Bar | Width transition (smooth updates) |
| Pulse | Critical notifications | Attention-getting effect |

### 17.11 Implementation Recommendation

**Phase 1: Foundation**
1. Text rendering (already exists per UI framework docs)
2. Rectangle primitive
3. Basic Button
4. Panel

**Phase 2: Core Components**
1. Icon/Image rendering
2. Progress Bar
3. Scrollable Container
4. List Item

**Phase 3: Complex Components**
1. Tab Container
2. Tree View
3. Dropdown Button
4. Toast + Toast Stack

**Phase 4: Specialized**
1. Modal Dialog
2. Tooltip system
3. Context Menu
4. Minimap

---

## 18. Implementation Priority

### Phase 1: Core Gameplay UI
1. Game viewport + camera controls + zoom controls
2. Top bar (date/time, speed controls, menu)
3. Colonist list (compact, clickable)
4. Inspect panel (basic colonist/building info)
5. Gameplay bar (Actions, basic Build)
6. Pause/unpause system

### Phase 2: Information Systems
1. Minimap with zoom controls
2. Resources panel (expandable tree view)
3. Notifications (toast system)
4. Production bills system

### Phase 3: Management Depth
1. Work priorities screen
2. Schedule screen
3. Research screen
4. History/log

### Phase 4: Polish
1. Tooltips throughout
2. Hotkey customization
3. Colonist Details Modal
4. Tutorial hints

### Phase 5: worldsim Differentiators
1. Memory visualization mode
2. Colonist knowledge panel
3. Social knowledge network view
4. Continuous pathfinding visualizations

---

## 19. Related Documentation

- [UI Framework Architecture](/docs/technical/ui-framework/INDEX.md) — Technical implementation
- [Rendering Boundaries](/docs/technical/ui-framework/rendering-boundaries.md) — What renders where
- [Colonist Memory System](/docs/design/systems/colonist-memory.md) — Memory mechanics
- [Needs System](/docs/design/systems/needs-system.md) — Colonist needs affecting UI
- [Game Start Experience](/docs/design/features/game-start-experience.md) — Pre-gameplay UI flow
- [Entity Placement System](/docs/technical/entity-placement-system.md) — How entities are placed in world

---

## Appendix A: Comparative Analysis

### RimWorld UI Patterns (Reference)

**Adapted for worldsim:**
- Colonist bar with at-a-glance mood indicators → Colonist list (compact)
- Architect menu with category hierarchy → Gameplay bar with Actions/Build
- Bill/queue system for production buildings → Kept
- Work priority grid → Kept

**Key Differences:**
- RimWorld requires extensive designation (cut trees, haul items, hunt animals)
- worldsim colonists are autonomous—they do these things themselves
- Our Actions menu is for **overrides**, not normal operations

### Oxygen Not Included UI Patterns

**Adapted for worldsim:**
- Resources panel with expandable detail → Resources tree view
- Duplicant vitals sidebar → Colonist list (simpler)

### Dwarf Fortress UI Patterns

**Adapted for worldsim:**
- Hotkey system for location bookmarks → Kept

---

## Appendix B: Control Reference

### Global Hotkeys

| Key | Action |
|-----|--------|
| Space | Pause/Unpause |
| 1/2/3 | Game speed |
| Escape | Cancel action / Close panel |
| ` | Toggle dev console |

### Camera

| Key | Action |
|-----|--------|
| WASD | Pan camera |
| Mouse wheel | Zoom |
| Home | Reset zoom |
| Shift+1-9 | Set bookmark |
| 1-9 | Jump to bookmark |
| Backspace | Return to previous location |

### Selection

| Key | Action |
|-----|--------|
| Click | Select entity |
| Shift+Click | Add to selection |
| Ctrl+Click | Remove from selection |
| Drag box | Box select |
| Double-click | Select all of type in view |

### Gameplay Bar

| Key | Action |
|-----|--------|
| A | Actions menu |
| B | Build menu |
| Q | Last used item |
| R | Rotate placement |

### Management Screens

| Key | Action |
|-----|--------|
| W | Work priorities |
| S | Schedule |
| H | History/Log |
| M | Memory overlay (when colonist selected) |
| L | Toggle colonist labels |

---

## Appendix C: Accessibility Considerations (Deferred)

**Status:** Deferred — These features are important but will be addressed in a later phase.

### Visual Accessibility

- Color-blind friendly palette options
- Icon shapes distinct beyond color alone
- Adjustable UI scale (75%-150%)
- High contrast mode option

### Input Accessibility

- Full keyboard navigation
- Rebindable hotkeys
- Mouse-only play possible (all actions accessible via clicks)
- Pause-friendly (all commands work while paused)

### Information Accessibility

- Screen reader hints for critical alerts
- Text alternatives for all icons
- Verbose tooltip option with more detail
