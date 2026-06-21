# In-game

![In-game HUD](../mocks/in-game.png)

HUD anatomy:

![In-game HUD regions](../mocks/in-game-anatomy.svg)

The colony management HUD. Everything the player reads and does during a session lives here: the world view, colonist roster, selected-entity panel, minimap, storage readout, task queue, and command bar. A modal dossier dialog covers the screen when the player double-clicks a colonist.

Reached from [Main Menu](./main-menu.md) via "Continue," or from [Landing site](./landing-site.md) after "Begin Descent." The "Menu" button in the HUD top-right returns to [Main Menu](./main-menu.md).

---

## Layout overview

The HUD is an absolute-positioned overlay on top of the faux 2D world canvas. Every panel is independently positioned; nothing uses a layout grid at the screen level.

```
[TOP BAR]                              52px, full width
[ROSTER]          ← 208px wide, top: 64px, left     (stacks vertically)
[INFO PANEL]      ← 320px wide, bottom-left corner
                  [MINIMAP]            232px wide, top-right column
                  [STORAGE]            "
                  [TASKS]              "
[ZOOM]            ← 34px, bottom-right
[TOASTS]          ← 280px, bottom-right above zoom
[COMMAND BAR]     ← centered, bottom
```

---

## World view

The background beneath all HUD panels is a simulated 2D tile view. In the prototype this is a set of absolutely positioned CSS divs that establish terrain regions, a river strip, scattered tree/bush sprites, the crashed ship, and the selected colonist indicator. A tile grid overlay (48×48px cells, `--line-hairline` at 40% opacity) subdivides the world. A vignette radial gradient darkens the edges.

The selected colonist shows as a white dot with an amber pulsing ring (scale oscillates between 1.0 and 1.15 over 2s) and a name label below in mono.

The crashed ship is rendered as a `rocket` icon (26px, amber) with an amber radial glow, rotated −24°, positioned at roughly 44% left / 50% top.

---

## Top bar

52px tall, full width, `--z-panel`, semi-transparent (`--bg-base` fading to ~86% transparent at the bottom edge, backdrop-filter blur 6px), hairline bottom border.

Three-column grid: `1fr auto 1fr`.

**Left — colony identity**

- `◈` glyph (20px, amber, amber glow)
- Colony name: "Hollow Reach" (Chakra Petch, 600, `--fs-md`, `--text-bright`)
- Sub-line: "3 survivors · Sol 14" (mono, `--fs-2xs`, `--text-faint`)

**Center — clock + speed controls**

The clock displays:
- Day number: "Day 14" (Chakra Petch, 600, `--fs-md`, `--text-bright`)
- Season badge: `Badge` tone `data`, label "Late Spring"
- Time: "09:42" (mono, `--fs-md`, `--data-bright`)

Below (or beside) the clock sits a speed control strip: four `<button>` elements inside an inset pill container. Speeds in order: pause, play, fast, very fast. The active speed button is amber-filled with amber glow; others are `--text-dim` on `--bg-inset`.

**Right — alerts + menu**

- Alert button: amber alert icon (16px), small crit-red badge with count "1" (9px mono, positioned top-right of button). Color: `--status-warn` normally, badge background `--status-crit`.
- "Menu" button: `Button` variant `secondary`, size `sm`, icon `menu`. Navigates to `go("menu")`.

---

## Colonist roster (left strip)

Positioned 64px from the top, 12px from the left, `--z-panel`, 208px wide. A vertical stack of colonist cards with `var(--space-2)` gap.

Each card is a `<button>` with a 2px amber left border when active (transparent otherwise), `--bg-panel` base → `--bg-panel-raised` when active.

Card interior — horizontal flex:
- `Avatar` (30px, mood-tinted, selected ring when card is active)
- Info column (flex: 1):
  - Name row: first name left (Chakra Petch, sm, `--text-bright`), mood percentage right (mono, 2xs, color from `moodColor()`)
  - `Meter` (inline, tone `accent`, size `sm`): label = current task, `valueText` = task progress %. This meter shows inline with the label and value on one row.

**Single click** — selects the colonist, updating the Info Panel.

**Double-click** — selects the colonist and opens the Colonist Details Dialog.

The tooltip on each card reads "Double-click for full dossier."

`moodColor()`: crit (< 0.3), warn (0.3–0.54), ok (≥ 0.55).

---

## Info panel (bottom-left)

320px wide, 12px from bottom and left, `--z-raised`. A `Panel` (accent `accent`, corners, flush) showing the selected colonist's compact dossier plus four quick tabs.

### Header

Flex row:
- `Avatar` (52px, mood-tinted)
- Text column: full name (Chakra Petch, `--fs-md`, 600), then two inline `Meter` rows: Mood (tone `auto`), current task (tone `accent`)
- Icon buttons: eye icon (opens full dossier dialog), close icon

### Tabs

Four `Tabs`: Needs (heart), Bio (user), Gear (box), Log (list). Default: Needs.

### Needs tab

A list of need rows. Each row: icon (13px, `--text-dim`) + `Meter` (label = need name, tone `auto`, size `sm`, value 0–1, percentage valueText). Mock needs: Food, Rest, Water, Recreation.

### Bio tab

A short italic paragraph (`--fs-sm`, loose line height, `--text`). Mock text for Mara: "Flight engineer, 34. Steady hands, sleeps poorly. Took the expedition to outrun a debt she won't discuss."

### Gear tab (compact HUD variant)

A compact read-out, not the full paper-doll. Shows:

- Armed status: current item (icon + name); if present, a hint in `--text-faint` shows hand class (2H / 1H / •) and "both hands" or "one hand." If nothing held: "Unarmed."
- Belt row (if any belt slots filled): "BELT" label + icon chips for each filled slot.
- Carry `Meter` (tone `data`, size `sm`): label "Carry," value = `totalCarryKg / CARRY_CAP_KG`, valueText = "X.X / 35 kg."
- Pack chips: a wrapping flex row of small chip elements (icon + optional quantity badge) for each item stack in the pack. Chip tooltip = "name · count · kg."

This is a read-only summary. The full gear paper-doll is in the Colonist Details Dialog.

### Log tab

A short chronological log (mono, `--fs-xs`, `--text-dim`). Mock entries:
- 09:40 — Started Foundation in Sector 4
- 09:12 — Hauled Wood ×4 to Stockpile A
- 08:30 — Ate a simple meal

### Actions row

Below the tab body, a hairline top border, then three small buttons: Draft (data tone, icon `bolt`), Go to (secondary, icon `crosshair`), Priorities (ghost, icon `list`).

---

## Right stack

232px wide, positioned 60px from top and 12px from right, `--z-panel`. Three panels stacked with `var(--space-2)` gap: minimap, storage, tasks. Storage and tasks have a collapse toggle in their panel header.

### Minimap (Region panel)

Panel: title "Region," accent `data`, corners, compact, flush.

The map body is an inline SVG (`viewBox="0 0 100 64"`, `preserveAspectRatio="xMidYMid slice"`) that shows a stylized top-down terrain sketch: land blobs, a water region, a river path, a faint grid, and several map markers.

SVG content:
- Background fill `#0c130e`
- Two land ellipses (dark greens)
- Water polygon (blue-gray)
- River path (stroke only, blue-gray, `opacity 0.7`)
- Grid lines (`--line-hairline` 22% opacity)
- Crashed ship indicator: amber rotated rectangle at ~42% x / 30% y
- Resource markers: three green circles (trees or resource nodes)
- Colonist dots: three white circles for on-map colonists
- Current view box: amber hairline rectangle showing the camera viewport

The viewport box (`rect x=36 y=22 width=26 height=20, stroke --accent-bright, strokeWidth 0.8`) indicates where the 2D world camera is currently pointing.

**Coordinate label** — absolute bottom-left of the map body, mono, `--data-bright`: "14.2°N · 9.8°W."

**Off-map colonist markers** — any colonist outside the current map view gets an arrow + distance label projected onto the minimap's inner perimeter. The projection formula:

1. Convert the colonist's `bearing` (0 = north, clockwise degrees) to a unit vector: `dx = sin(bearing), dy = -cos(bearing)`.
2. Scale to reach the inset perimeter: `half = 0.5 - 0.12 = 0.38`; `scale = half / max(|dx|, |dy|)`; position = `(0.5 + dx·scale, 0.5 + dy·scale)` in unit space, then multiply by 100 for percentage.
3. Place a `chevronUp` icon (12px, amber, amber glow drop-shadow) at that point, rotated by `bearing` degrees so it points outward. A distance label (`--fs-2xs` = 9px, mono, amber) appears below the arrow.

Mock off-map colonists: Dex at bearing 40°, 180m; Joon at bearing 118°, 95m; Vale at bearing 318°, 240m.

### Storage panel

Panel: title "Storage," accent `data`, corners, compact. Collapse toggle (chevronUp/Down) in header actions.

When open: a list of resource rows. Each row: icon (14px, `--text-dim`), name (flex: 1, `--fs-sm`), count (mono, `--fs-sm`, bold, `--text-bright`). Hover highlights the row with `--bg-hover`.

Mock resources: Wood 142, Stone 88, Food 36, Metal Scrap 21, Plant Fiber 54.

### Tasks panel

Panel: title "Tasks," kicker "{N} queued" (e.g. "1,284 queued"), corners, compact. Collapse toggle in header actions.

When open: a scrollable list (`max-height: 230px, overflow-y: auto`) of single-line task rows, plus a sticky footer showing the overflow count.

Each task row: a 5×5px status dot, a main cell (task label + detail in same row, truncated), and an optional colonist name right-aligned.

**Status dot colors:**
- `active` — `--status-ok` with glow
- `pending` — `--status-warn`, no glow
- `blocked` — `--status-crit` with glow

The task list shows only the top-priority tasks; the sticky overflow line reads "+{N} more · filter to refine." The `TASK_TOTAL` in the prototype is 1,284 (showing that this list must scale to thousands; the shown rows are a filtered window).

Mock tasks (abbreviated): Build Foundation (active, Mara), Haul Wood ×6 (active, Idris), Harvest Reed (active, Rin), Cook Meals ×4 (blocked — needs Campfire), Mine Stone (pending), …

---

## Zoom controls

Three stacked square buttons (34×34px each, `--bg-panel`, `--line-edge` border) anchored bottom-right (above the right edge of the toasts column). Icons: plus, home (reset zoom), minus. Hover turns border amber and icon amber-bright.

---

## Notification toasts

280px wide, bottom-right, 64px from the bottom, `--z-toast`. A vertical stack (gap `var(--space-2)`) of toast cards that slide in from the right.

Each toast: flex row with a 3px accent left border (tint by severity: info = `--status-info`, warn = `--status-warn`, crit = `--status-crit`), an icon (matching severity color), a text column (title in Chakra Petch + body in `--fs-xs`), and a close button.

Closing a toast removes it from the displayed list (tracks dismissed indices in local state).

Slide-in animation: `opacity 0 + translateX(16px)` → normal, `--dur`.

Mock notifications:
- "Construction complete" / "Foundation in Sector 4 is finished." (info)
- "Idris is hungry" / "Food need is low. No prepared meals available." (warn)
- "Storm front inbound" / "Seasonal storm in ~2 days. Secure loose cargo." (crit)

---

## Command bar

Centered, bottom, 12px from the bottom edge, `--z-panel`. A horizontal pill (`--bg-base`, `--line-edge` border, `--shadow-pop`, `--radius-md`, padding `var(--space-2)`).

Four command category buttons, each with an icon + label + chevron-up caret. Clicking a category button toggles its popup menu (clicking again or clicking another category collapses it). Only one menu is open at a time.

**Categories:**
- Orders (bolt): Mine, Chop Wood, Harvest, Haul, Cancel
- Zones (layers): Stockpile, Growing, Dumping
- Build (hammer): Foundation, Wall, Door, Floor
- Production (box): Campfire, Crafting Spot, Shelf

**Active state** — the button background goes to `--bg-active`, label to `--accent-bright`.

**Popup menu** — floats 8px above the button, left-aligned, `--bg-panel-raised`, `--line-edge` border, `--shadow-pop`. Each item row: icon (14px) + label. Hover: `--bg-active`, `--accent-bright`. Enters with `menuRise` (opacity + translateY(6px) → normal, `--dur-fast`).

**Keyboard hint** — right of the category buttons, a divider then small text showing two `KeyCap` shortcuts: `B` build · `Space` pause.

---

## Colonist details dialog

![Colonist dossier](../mocks/in-game-dossier.png)

![Gear paper-doll, Gear tab](../mocks/in-game-dossier-gear.png)

Dossier anatomy:

![Colonist dossier regions](../mocks/colonist-dossier-anatomy.svg)

Opened by double-clicking a colonist card (roster or info panel eye button). A `Dialog` (size `lg`, modal) using the design-system Dialog primitive.

> **Implementation status (in-game C++ dossier).** The dialog shell, persistent header, kicker/footer, 8-tab bar, and the Bio / Health / Memory / Tasks tab layouts are built and match the prototype at the element level. Remaining gaps are tracked under the **In-game dossier fidelity** epic in `/docs/status.md`:
> - **Gear tab — not implemented to the prototype.** The in-game tab is a sparse hands + inventory readout; the full paper-doll (3-column worn slots + figure + hand slots, weight-based pack, belt slots, carry meter) is **not built**. This is the biggest remaining gap.
> - **Skills, Social, Log — placeholders.** Rendered as honest "not yet simulated" empty states; no backing systems exist yet (no skills, relationships, or activity-log systems).
> - **Bio traits** render as plain text, not `Badge` chips. **Tasks** current-task panel is simpler than the prototype (no progress bar / type / distance breakdown). **Memory** entity rows show map coordinates, not distance-from-colonist, and "Threats" is a placeholder category.
> - **Footer:** Close works; **Work Priorities** and **Draft** are visual-only (no backing systems).
> - The whole dialog renders small relative to the prototype — see the **UI Scale setting** epic, not a dossier bug.
> - Not yet stress-tested for text overflow with a **data-rich colonist** (long backstory, full gear, many known entities/tasks); only placeholder data has been exercised.

### Header area

Avatar (72px, mood-tinted) beside a 2×2 `Stat` grid: Role, Origin, Age (unit "yrs"), Mood (percentage, tone crit if < 0.4, else ok). Dialog kicker: "Personnel File · {role}". Dialog title: colonist name.

### Tabs (8)

Bio (user), Health (heart), Skills (hammer), Social (users), Gear (box), Memory (eye), Tasks (list), Log (clock).

The tab set mirrors the real colonist systems (see the data notes in the prototype's `mock.ts`). Health folds in the former "Needs" tab, since in-engine health *is* the needs + mood system.

#### Bio tab

Backstory paragraph (`--fs-md`, `--lh-loose`). `Divider` "Traits." Trait `Badge` chips (same tone logic as party selection). Current task note: hammer icon + "Currently: {task}" in amber.

#### Health tab

![Health tab](../mocks/in-game-dossier-health.png)

The colonist's Needs + Mood system. A full-width `Meter` for Mood (tone `auto`) at the top, captioned that mood is computed from the needs below. Then the eight needs in a 2-column grid; each row is an icon (14px, `--text-dim`) + `Meter` (size `sm`) colored against that need's *own* thresholds: `crit` below its critical threshold, `warn` below its seek threshold, else `ok`.

Two groups: **Vital Needs** (Hunger, Thirst, Energy, Bladder, Digestion — the five the AI acts on today) and **Comfort** (Hygiene, Recreation, Temperature — tracked but not yet pursued, rendered slightly dimmed), with a note that colonists don't act on comfort needs yet.

A **Body & Ailments** divider closes the tab over a dashed empty-state panel ("No injuries or ailments / Wounds, illness, and treatment arrive with the medical update") — the injury/medical system isn't built yet.

#### Skills tab

2-column grid. Each cell: icon (14px, `--text-dim`), skill name (flex, `--fs-sm`), fixed-width `Meter` (120px, tone `accent`, value = level/20, valueText = level). Scale 0–20.

Skills per colonist match the `COLONISTS` mock data (same as party selection screen).

#### Social tab

A list of relationship rows. Each row: `Avatar` (36px) + name/relationship text column + a `Badge` with the relationship score.

Mock relationships (Mara's perspective):
- Idris Okonkwo — "Crewmate · Respected" — Badge `ok` "+24"
- Rin Calloway — "Crewmate · Acquaintance" — Badge `default` "+6"

Trailing muted note: "Relationships deepen as the colony shares meals, work, and close calls."

#### Gear tab

The full paper-doll view. See [equipment.md](../../game-systems/colonists/equipment.md) for the inventory and equipment model that this tab visualizes. The visual structure:

**Paper-doll** — three-column grid (`1fr 96px 1fr`). Left column: worn slots (Head, Face, Body Over, Body Under, Legs, Feet). Center column: an SVG stick figure (viewBox 0 0 64 150, circle head + torso + arm + leg paths). Right column: worn slots (Back, Belt) and hand slots.

Hand slots: if `HELD.twoHanded` is set, a single "Both Hands" slot spanning the column (slightly amber-tinted border). Otherwise, separate "Left Hand" and "Right Hand" slots. A hand-class chip (`2H` / `1H` / `•`) appears in amber on held items.

Empty slots use a dashed `--line-hairline` border. Filled slots use a solid `--line-edge` border with item icon (amber) and name.

**Pack section** (below paper-doll) — titled with the back container's equipped item name (e.g. "Field Pack"), weight readout "X.X / 30 kg" right. `Meter` (tone `data`, shifts to `warn` above 85%). Caption: "Weight-based · pocket & one-hand items." Then a list of item stacks (each row: icon, name, `×qty` if qty > 1, hand-class tag, weight in kg right-aligned).

**Belt section** — titled with belt item name (e.g. "Tool Belt"), slot readout "X / 4 slots · X.X kg" right. Caption: "Quick-draw slots · one-hand tools only." Then a 40×40px grid of belt cells. Filled cells: icon (18px, amber). Empty cells: dashed border, plus icon (`--text-faint`).

**Total carry load** — full-width `Meter` at the bottom (label "Carry load," value = total/cap, tone `data` below 85%, `warn` above, valueText "X.X / 35 kg").

#### Memory tab

![Memory tab](../mocks/in-game-dossier-memory.png)

The colonist's personal, line-of-sight knowledge (the `Memory` component, populated by the vision system). A summary row at top shows two metrics: locations known (total) and sight range (meters). Below, a 2-column grid of capability categories — **Food Sources**, **Water Sources**, **Resources**, **Colonists**, **Threats** — each with an icon, name, and count `Badge`. Under each category, a sample of known entities: name + distance (mono, `--data-bright`) + last-seen freshness (mono, `--text-faint`; stale entries read "may be gone" in `--status-warn`), with a "+N more" line when the list is truncated. Empty categories read "None sighted."

Trailing muted note: memory is personal (not the colony's shared map) and goes stale until revisited.

#### Tasks tab

![Tasks tab](../mocks/in-game-dossier-tasks.png)

The colonist's work, drawn from the colony goal registry. A prominent **Currently** panel (amber left border) shows the active, self-assigned task: type (Chakra Petch), target, nav state + distance (mono), and a progress `Meter`. Below a "Known Work" `Divider`, a list of known goal-tasks — each row: icon, label + status detail (e.g. "0/2 metal"), distance, and a status `Badge`: Available (`ok`), In Progress (`data`), Blocked (`crit`), Waiting (`warn`).

Trailing muted note: colonists pick the highest-priority job they know about on their own; per-colonist preferences live under Work Priorities (the planned control wired to the footer button).

#### Log tab

Chronological mono log entries (`--fs-xs`, `--text-dim`). Mock entries (newest first):
- 09:40 — Started Foundation in Sector 4
- 09:12 — Hauled Wood ×4 to Stockpile A
- 08:30 — Ate a simple meal
- 07:55 — Woke up (slept on the ground, −4 mood)
- 06:10 — Finished repairing the salvage cutter

### Dialog footer

Three buttons: Close (ghost, sm), Work Priorities (secondary, sm, icon `list`), Draft (primary, sm, icon `bolt`).

---

## Notes on equipment model

The gear tab visualizes the equipment model; for the underlying rules (hand classes, weight-based pack vs. slot-based belt, two-hand item exclusivity, worn apparel vs. carry capacity) see [equipment.md](../../game-systems/colonists/equipment.md). The summary: `hands = 0` items are pocket items (fit pack or belt pocket), `hands = 1` items are one-hand tools (fit hand slot, pack, or belt slot), `hands = 2` items occupy both hand slots and cannot be stowed.
