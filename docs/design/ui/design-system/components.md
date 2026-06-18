# Salvage — component catalog

The fourteen primitives the Salvage UI is built from, extracted from the React prototype under `docs/ui-prototype/` so the design survives without the prototype code. This is the design contract: what each component is, every variant and prop, the visual states, the parts and the tokens that drive them, and the behavior that would vanish if you only kept screenshots. Numeric values live in [tokens.md](./tokens.md); this doc names tokens and links there rather than restating pixels. For the look and feel these primitives serve, see [design-language.md](../design-language.md).

Everything here is the contract, not the implementation. No C++ mapping, no "how to build it in the engine"; that's a separate later doc.

The primitives:

- [Button](#button)
- [Panel](#panel)
- [Modal](#modal)
- [Badge](#badge)
- [Avatar](#avatar)
- [Icon](#icon)
- [Meter](#meter)
- [Slider](#slider)
- [Stat](#stat)
- [Tabs](#tabs)
- [SegmentedControl](#segmentedcontrol)
- [Tooltip](#tooltip)
- [Divider](#divider)
- [KeyCap](#keycap)

---

## Button

**Purpose** — the interactive action element; the only clickable text/icon control in the system.

**Variants** — five, each a distinct fill/border/glow treatment:

- `primary` — amber fill, vertical gradient from `--accent-bright` down to `--accent`, text in `--accent-contrast` (near-black), border `--accent-bright`. The loud "do this" button.
- `secondary` — outlined glass. Background `--bg-panel-raised`, text `--text-bright`, hairline border `--line-edge`. The default; this is what you get if you pass no variant.
- `ghost` — no chrome at all until hover. Transparent fill and border, text `--text`. For toolbar icons and low-emphasis actions.
- `danger` — destructive. Transparent fill, text and border in `--status-crit` (the border at 50% mix toward transparent). Reserved for irreversible actions.
- `data` — teal holographic. Fill is `--data` at 12% over transparent, text `--data-bright`, border `--data` at 45%. The read-only/diagnostic-action counterpart to primary.

**Sizes** — `sm`, `md` (default), `lg`. Size sets height, horizontal padding, and font size (all from tokens), and also the inline icon glyph size: 13px at `sm`, 15px at `md`, 18px at `lg`. Icon-only buttons become square at the matching height.

**Props**

| Prop | Type / values | Meaning |
|------|---------------|---------|
| `variant` | `primary \| secondary \| ghost \| danger \| data` | visual treatment; defaults to `secondary` |
| `size` | `sm \| md \| lg` | height/padding/font and the glyph size; defaults to `md` |
| `icon` | `IconName` | leading icon, rendered before the label |
| `iconRight` | `IconName` | trailing icon, rendered after the label |
| `iconOnly` | `boolean` | square button, label text suppressed; pass `aria-label` for the name |
| `block` | `boolean` | full-width, `display: flex` instead of inline |
| `stencil` | `boolean` | uppercase, letter-spaced signage label in the display font |
| `children` | `ReactNode` | the label content |
| ...rest | `ButtonHTMLAttributes` | native button attrs pass through (`disabled`, `onClick`, `aria-label`, etc.) |

**States**

- Normal — variant fill as above.
- Hover (only when not disabled) — per variant: `primary` gains a 16px `--accent-glow` bloom and its gradient brightens to white→`--accent-bright`; `secondary` border goes `--accent`, text `--accent-bright`, fill `--bg-active`; `ghost` fills `--bg-hover`, text `--text-bright`; `danger` fills `--status-crit` at 14%, border solid `--status-crit`, text white; `data` border goes solid `--data` with a 14px `--data-glow` bloom.
- Pressed — `translateY(1px)`, the whole system's tactile nudge-down.
- Disabled — opacity 0.4, `not-allowed` cursor, no shadow, no hover response.

**Anatomy** — an inline flex row, gap `--space-2`, with optional leading icon, label span, optional trailing icon. Border `--border-width`, radius `--radius-ui`, body font `--font-ui` at weight 600. Stencil swaps the label to `--font-display`, uppercase, `--ls-wide`. Transitions run on `--dur-fast` for color/border/transform and `--dur` for the glow box-shadow, both on `--ease`.

**Behavior** — the `stencil` flag only restyles the label; it does not change layout. `iconOnly` drops the label and forces a square footprint at the size's height. The glyph-size-from-size mapping (13/15/18) is the one bit of logic worth preserving: icons scale with the button, they are not a fixed size.

---

## Panel

**Purpose** — the framing surface for everything; titled, bracketed container that holds content and other primitives.

**Variants** — three surfaces:

- `panel` (default) — `--bg-panel` over a `--line-hairline` border, with the standard `--panel-shadow`.
- `raised` — `--bg-panel-raised`, border lifts to `--line-edge`. For floating cards and modals.
- `inset` — `--bg-inset`, hairline border, `--shadow-inset` instead of the drop shadow. Sunken; for wells, code blocks, data readouts.

**Accents** — `accent` (default), `data`, `none`. The accent colors the kicker text and the corner brackets: amber `--accent`, teal `--data`, or `--line-strong` for `none`.

**Props**

| Prop | Type / values | Meaning |
|------|---------------|---------|
| `title` | `ReactNode` | heading shown in the header bar |
| `kicker` | `ReactNode` | small stencil label above the title |
| `actions` | `ReactNode` | slot at the header's right edge (buttons, etc.) |
| `variant` | `panel \| raised \| inset` | surface tint and shadow; defaults to `panel` |
| `accent` | `accent \| data \| none` | kicker and corner-bracket color; defaults to `accent` |
| `corners` | `boolean` | draw the four L-bracket corner ticks; defaults to `true` |
| `scanlines` | `boolean` | apply the `fx-scanlines` CRT overlay |
| `glow` | `boolean` | accent-colored bloom on the border |
| `compact` | `boolean` | tighter header and body padding for dense HUD panels |
| `flush` | `boolean` | remove body padding entirely (edge-to-edge content like lists/maps) |
| `children` | `ReactNode` | body content |
| `className` | `string` | extra class on the root section |
| `bodyClassName` | `string` | extra class on the body div |
| `style` | `CSSProperties` | inline style on the root |

**States** — Panel is static chrome; it has no hover/pressed states of its own. Its visible "states" are the flag combinations: glow on/off, scanlines on/off, corners on/off, compact, flush. The header only renders if `title`, `kicker`, or `actions` is present.

**Anatomy** — a positioned `section` with up to four absolutely-positioned corner spans, an optional header (`headText` column of kicker + title, plus an `actions` group), and a body. The header sits above a `--line-hairline` bottom border; its padding is `--space-3` × `--space-4`, scaled by the `--density` multiplier. The body padding is `--space-4` × `--density`. Title is `--font-display` at `--fs-md`, weight `--title-weight`, `--title-spacing`, uppercased, truncated with ellipsis. Kicker is `--label-font` at `--fs-2xs`, `--label-spacing`, uppercased, colored by accent.

**Behavior / geometry**

- Corner brackets — four 12px square spans, one per corner, each offset `-1px` so they straddle the panel's own border. Each draws only its two relevant edges at `--bw-thick` (top+left for `tl`, top+right for `tr`, and so on), colored by accent. They are `pointer-events: none`, z-index 2, and their `display` is driven by `--corner-display` so the whole set can be killed from one token. Suppress per-panel with `corners={false}`.
- Glow — adds two layered shadows on top of `--panel-shadow`: a 1px ring of the accent at 30% mix, plus a 26px bloom of `--accent-glow` (or `--data-glow` for the data accent). Only the `accent` and `data` accents glow; `none` has no glow rule.
- Scanlines — opt-in via the shared `fx-scanlines` class (the 1px-on/2px-off overlay from the design language), not a Panel-specific style.
- Compact — overrides header and body padding to `--space-1-5` × `--space-3` (× density). Use for stacked HUD panels where the default padding wastes space.
- Flush — body padding becomes 0, so content can run to the panel's inner edge. Compose with `actions` for a titled-but-edge-to-edge panel (this is exactly how Dialog uses it).

---

## Dialog

**Purpose** — a centered, scrimmed dialog built on Panel; the system's blocking overlay.

**Sizes** — `sm`, `md` (default), `lg`, setting the dialog width. The wrap also caps at 92% width and 86% height of the viewport so it never overflows.

**Accents** — `accent` (default) or `data`, passed straight through to the underlying Panel.

**Props**

| Prop | Type / values | Meaning |
|------|---------------|---------|
| `open` | `boolean` | mounts/unmounts the dialog; renders nothing when false |
| `onClose` | `() => void` | called on scrim click, close-button click, or Escape |
| `title` | `ReactNode` | Panel title |
| `kicker` | `ReactNode` | Panel kicker |
| `size` | `sm \| md \| lg` | dialog width; defaults to `md` |
| `accent` | `accent \| data` | Panel accent; defaults to `accent` |
| `footer` | `ReactNode` | optional footer row, right-aligned actions |
| `children` | `ReactNode` | dialog body |

**States** — open or not mounted. On open the scrim fades in (`--dur`, `--ease`) and the dialog pops (`--dur`, `--ease-out`) from `scale(0.96) translateY(8px)`. The close button is `--text-dim`, brightening to `--text-bright` over `--bg-hover` on hover.

**Anatomy** — a full-bleed scrim (`inset: 0`, `--scrim` background, 2px backdrop blur, z-index `--z-modal`, centered grid) wrapping the dialog. The dialog is a Panel rendered with `glow`, `scanlines`, and `flush` always on, plus a close button in the Panel's `actions` slot. Inside the flush Panel body: a scrolling `content` region padded `--space-5`, and an optional `footer` separated by a `--line-hairline` top border, padded `--space-3` × `--space-5`, actions right-aligned with `--space-3` gaps.

**Behavior**

- Escape to close — a `keydown` listener is attached on the window in the capture phase, and it calls `stopImmediatePropagation()` before `onClose`. The capture-phase + stop-immediate combination is deliberate: it lets Escape close the modal without also tripping the dev-chrome toggle bound on the same window. Preserve this ordering.
- Scrim click closes; clicks inside the dialog call `stopPropagation()` so they don't bubble to the scrim.
- The fixed-on composition (glow + scanlines + flush) means a Dialog is always a glowing, CRT-textured, edge-to-edge Panel; those three are not configurable here.

---

## Badge

**Purpose** — a small inline status/label pill.

**Tones** — seven. `default` (raised surface, dim text), `outline` (transparent with an edge border), `accent`, `data`, `ok`, `warn`, `crit`. The four colored tones (`accent`/`data`/`ok`/`warn`) use a 14% color-mix fill with a 45% border; `crit` runs slightly hotter at 16% fill and 50% border. Text color is the tone's bright/base variant.

**Props**

| Prop | Type / values | Meaning |
|------|---------------|---------|
| `children` | `ReactNode` | label text |
| `tone` | `default \| accent \| data \| ok \| warn \| crit \| outline` | color treatment; defaults to `default` |
| `icon` | `IconName` | optional leading icon, rendered at 11px |
| `dot` | `boolean` | leading glowing status dot in the current color |

**States** — none interactive. Badge is a static readout.

**Anatomy** — an inline flex row, fixed 20px tall, padded `--space-2`, gap 5px, radius `--radius-sm`, `--bw` border. Text is `--fs-2xs`, weight 600, `--ls-wide`, uppercased. The optional dot is a 6px `--radius-pill` circle filled with `currentColor` and a 6px `currentColor` glow, so it always matches the badge's text color. An icon renders at 11px inline.

**Behavior** — `dot` and `icon` can both appear; order is dot, then icon, then label. The dot's glow keys off `currentColor`, which is why it tracks the tone automatically rather than taking its own color prop.

---

## Avatar

**Purpose** — a deterministic colonist portrait: a generated silhouette plus initials, framed like a dossier photo, with an optional mood ring.

**Props**

| Prop | Type / values | Meaning |
|------|---------------|---------|
| `seed` | `string` | the colonist's name; drives both color and initials |
| `size` | `number` (px) | frame width and height; defaults to 44 |
| `mood` | `number` 0..1 | tints the frame ring and adds a glow; omit for a neutral ring |
| `selected` | `boolean` | draws an accent outline around the frame |
| `className` | `string` | extra class on the frame |

**States**

- Default — neutral `--line-edge` frame, no ring glow.
- Mood set — frame border recolors to the mood color and gains an 8px glow at that color (the `55` alpha suffix is appended to the hex/var to soften it).
- Selected — a `--bw` `--accent` outline at 1px offset, on top of whatever ring is showing.

**Anatomy** — a `--bw-thick` framed box, radius `--radius-sm`, `--bg-inset` background, `overflow: hidden`. Inside: an SVG silhouette filling the frame, the initials pinned bottom-left (`--font-mono`, 9px, weight 700, with a dark text-shadow for legibility over the art), and a small L-bracket corner tick top-right (5px, `--text-faint`) for the dossier feel. The mood-driven border color and glow are inline styles, not tokens, because they're computed per instance.

**Behavior / formulas** — this is the part that must survive the prototype.

Seed hash (FNV-1a, 32-bit):

```
h = 2166136261
for each char c in seed:
    h = h XOR c.charCodeAt
    h = (h * 16777619) mod 2^32     // Math.imul, 32-bit wrap
h = h >>> 0                         // force unsigned
```

From that hash:

- `hue  = h mod 360`
- `hue2 = (hue + 40) mod 360`  — the second gradient stop, 40 degrees around the wheel.

The portrait art uses those two hues at fixed lightness/saturation:

- background `linearGradient` (top-left to bottom-right): stop 0 `hsl(hue 45% 24%)`, stop 1 `hsl(hue2 50% 14%)`.
- head circle and shoulders path: `hsl(hue 30% 60% / 0.55)`.
- initials text color: `hsl(hue 55% 82%)`.

So the same name always produces the same colors; the avatar is stable across sessions with no stored state. The SVG gradient id is derived from the hash too (`g` + `h.toString(36)`) to keep multiple avatars' gradients from colliding.

Initials — split the seed on whitespace, take the first character of each word, keep the first two, uppercase. "Kai Okafor" gives "KO"; a single-word seed gives one letter.

Mood ring thresholds (the `moodColor` mapping):

- `mood === undefined` → `--line-edge` (neutral, and no glow at all)
- `mood < 0.30` → `--status-crit`
- `mood < 0.55` → `--status-warn`
- `mood >= 0.55` → `--status-ok`

The glow only renders when `mood` is defined. (The gallery's separate mood-percent text label uses the same three thresholds, so a colonist's ring color and their printed mood swatch always agree.)

---

## Icon

**Purpose** — the line-icon set; a single component that renders any glyph by name.

Full glyph reference and the extracted path data live in [icons.md](./icons.md) and `icons.json`; this section is just the contract.

- 51 glyphs in the `IconName` union (the gallery shows 48; `shirt`, `pants`, and `boot` exist in the set but aren't in the gallery grid).
- 24×24 `viewBox`. Strokes use `currentColor` with round caps and joins, default `strokeWidth` 1.6, so icons inherit text color and any parent glow.
- 8 glyphs render filled rather than stroked by default: `play`, `fast`, `veryFast`, `energy`, `bolt`, `star`, `mountain`, `water`. Filled glyphs set `fill: currentColor` and `stroke: none`; everything else is the reverse.

**Props**

| Prop | Type / values | Meaning |
|------|---------------|---------|
| `name` | `IconName` | which glyph to render |
| `size` | `number` (px) | width and height; defaults to 16 |
| `strokeWidth` | `number` | stroke weight for stroked glyphs; defaults to 1.6 |
| `filled` | `boolean` | force fill/stroke; overrides the per-glyph default from the filled set |
| `className` | `string` | extra class |
| `style` | `CSSProperties` | inline style |

**Behavior** — `filled` is resolved as `filled ?? FILLED.has(name)`: an explicit prop wins, otherwise the eight-glyph filled set decides. The SVG is `aria-hidden`; semantic labeling is the caller's job (e.g. Button's `aria-label`).

---

## Meter

**Purpose** — a horizontal progress/level bar with an optional label and value readout.

**Tones** — `accent` (default), `data`, `ok`, `warn`, `crit`, and `auto`. The five fixed tones map straight to their color token. `auto` picks the color from the value (see formula).

**Sizes** — `sm`, `md` (default). Size sets the track height: thinner at `sm`. The inline variant uses its own slightly different heights.

**Props**

| Prop | Type / values | Meaning |
|------|---------------|---------|
| `value` | `number` 0..1 | fill fraction; clamped to [0, 1] |
| `label` | `string` | left-aligned stencil label |
| `valueText` | `string` | right-aligned mono value (the caller formats it, e.g. `"82%"`) |
| `tone` | `accent \| data \| ok \| warn \| crit \| auto` | fill color; defaults to `accent` |
| `segmented` | `boolean` | overlay notch ticks on the fill |
| `size` | `sm \| md` | track height; defaults to `md` |
| `inline` | `boolean` | render label and value INSIDE the bar instead of above it |
| `className` | `string` | extra class |

**States** — non-interactive. The fill animates its width on `--dur-slow` with `--ease-out` whenever `value` changes (the design language's "meters animate their fill" rule).

**Anatomy (stacked, default)** — a column, gap `--space-1`. Optional header row with the label (`--label-font`, `--fs-xs`, `--text-dim`, `--label-spacing`, uppercased, ellipsis-truncated) on the left and the value (`--font-mono`, `--fs-xs`, weight 600, colored by tone) on the right. Below it the track: `--bg-inset`, `--bw` `--line-hairline` border, `--radius-pill`, clipped. The fill is the tone color with an 8px glow of the same color, also pill-radiused.

**Behavior / formulas**

- Value clamp — `v = max(0, min(1, value))`; the fill width is `v * 100%`.
- `auto` tone thresholds (read exactly from `toneColor`):
  - `value < 0.25` → `--status-crit`
  - `value < 0.50` → `--status-warn`
  - `value >= 0.50` → `--status-ok`
  Note these differ from Avatar's mood thresholds (0.25/0.50 here vs 0.30/0.55 there); they are not the same scale, don't unify them.
- Segmented notches — an `::after` overlay over the whole track, a `repeating-linear-gradient` to the right: 7px transparent, then 2px of `--bg-panel` (the gap), repeating. So the bar reads as fixed 7px-wide segments separated by 2px gutters in the panel color, regardless of fill level. `pointer-events: none`.
- Inline variant — the track becomes a single 18px-tall element (16px at `sm`), `--radius-sm` instead of pill. The fill is drawn as a 22%-mix wash of the tone color with a solid 2px right edge in the full tone color and an 8px glow, so the leading edge reads as a bright bar against the dim wash. A separate overlay holds the label and value on top of the fill: label in `--text-bright`, value in the tone color, both at `--fs-2xs`. Both get a doubled `--bg-void` text-shadow (`0 1px 2px` plus `0 0 2px`) so the text stays legible whether it sits over filled or empty track. That text-shadow is the whole trick for legibility-over-fill; keep it.

---

## Slider

**Purpose** — a labeled range input with a value readout and an optional reference detent.

**Props**

| Prop | Type / values | Meaning |
|------|---------------|---------|
| `label` | `string` | the control's label (bound to the input via `htmlFor`) |
| `value` | `number` | current value (controlled) |
| `min` | `number` | range minimum |
| `max` | `number` | range maximum |
| `step` | `number` | snap increment; defaults to 1 |
| `unit` | `string` | appended to the default display (e.g. `"%"`) |
| `detent` | `number` 0..1 | normalized position of a reference marker on the track |
| `onChange` | `(value: number) => void` | fired with the numeric value on input |
| `format` | `(value: number) => string` | custom value formatter; overrides the default unit display |

**States** — the thumb scales to 1.15× on hover (`--dur-fast`, `--ease`). Pressed/drag is the native range behavior. Keyboard focus moves the thumb by `step`.

**Anatomy** — a column, gap `--space-2`. Header row: label (`--fs-sm`, `--text`, `--ls-wide`) left, value `output` (`--font-mono`, `--fs-sm`, weight 600, `--accent-bright`) right. Below, a 18px-tall `trackWrap` containing: the visual track (4px tall, `--bg-inset`, hairline border, `--radius-pill`, clipped), a fill (`--accent` with an `--accent-glow` bloom), the optional detent marker, and a transparent native `<input type="range">` overlaid full-width for the actual interaction. The thumb is restyled 14px, `--radius-sm` (a square, not a circle), `--accent-bright` with a `--bg-void` border and a 10px `--accent-glow`.

**Behavior / formulas**

- Fill width — `pct = ((value - min) / (max - min)) * 100`, applied as the fill's width percent.
- Value formatting — if `format` is given, the display is `format(value)`; otherwise it's `value` followed by a space and `unit` when `unit` is set, else just the number. The gallery's gravity slider uses `format={(v) => v.toFixed(2)+"g"}` to show `0.40g`; the population slider uses `unit="%"` for `62 %`.
- Step snapping — delegated to the native range input's `step`; the prototype doesn't re-snap in JS, so `step={0.01}` gives hundredths and `step={1}` gives integers.
- Detent marker — a 2px-wide `--data` (teal) vertical tick at 70% opacity, positioned by `left: detent * 100%` and centered with `translateX(-50%)`, inset 2px top and bottom, `pointer-events: none`. It's a visual reference only (the "ideal / earth-like" value), it does not snap or constrain the thumb. Rendered only when `detent` is defined.

---

## Stat

**Purpose** — a labeled numeric readout: a stencil label over a large value, with an optional small unit.

**Tones** — `default` (bright text), `accent`, `data`, `ok`, `warn`, `crit`. Tone colors only the value, not the label.

**Sizes** — `sm`, `md` (default), `lg`, setting the value's font size (`--fs-md` / `--fs-xl` / `--fs-3xl`). The label size is fixed.

**Alignment** — `left` (default), `right`, `center`; sets both flex alignment and text-align so a right-aligned stat reads cleanly in a right column.

**Props**

| Prop | Type / values | Meaning |
|------|---------------|---------|
| `label` | `string` | the stencil caption |
| `value` | `ReactNode` | the main value (string or node, e.g. `"1,204"`) |
| `unit` | `string` | small trailing unit appended to the value |
| `tone` | `default \| accent \| data \| ok \| warn \| crit` | value color; defaults to `default` |
| `align` | `left \| right \| center` | alignment; defaults to `left` |
| `size` | `sm \| md \| lg` | value font size; defaults to `md` |

**States** — non-interactive.

**Anatomy** — a column, 2px gap. Label is `--label-font`, `--fs-2xs`, `--label-spacing`, uppercased, `--text-dim`, no-wrap. Value is `--font-display`, weight 600, `line-height: 1`, baseline-aligned inline-flex with a 3px gap so the unit sits next to it. The unit is `--font-ui`, weight 500, `0.62em` (so it scales with the value's size), `--text-dim`. Value color comes from the tone class; default is `--text-bright`.

**Behavior** — `value` is a `ReactNode`, so callers pass pre-formatted strings (the component does no number formatting, that's why the gallery passes `"1,204"` with the comma already in). The unit's `em`-relative size means it always stays proportionally small whether the stat is `sm` or `lg`.

---

## Tabs

**Purpose** — an underline tab bar for switching between content views.

**Props**

| Prop | Type / values | Meaning |
|------|---------------|---------|
| `tabs` | `TabItem<T>[]` | the tabs; each `{ value, label, icon? }` |
| `value` | `T` | the selected tab's value |
| `onChange` | `(value: T) => void` | fired with the clicked tab's value |
| `className` | `string` | extra class on the bar |

`TabItem<T>` is `{ value: T; label: string; icon?: IconName }`. `T` is a string-literal union, so tab values are type-checked against the caller's set.

**States**

- Normal — `--text-dim`, transparent bottom border.
- Hover — text brightens to `--text-bright`.
- Selected (`active`) — text `--accent-bright`, bottom border `--accent`. Set by `aria-selected` and the active class when `value` matches.

**Anatomy** — a flex row, gap `--space-1`, sitting on a `--line-hairline` bottom border. Each tab is 34px tall, padded `--space-3`, `--font-display`, `--fs-sm`, weight 500, `--ls-wide`, uppercased, with a 2px transparent bottom border pulled up `-1px` (`margin-bottom: -1px`) so the active underline overlaps the bar's own border cleanly. Optional icon at 14px before the label. Color transitions on `--dur-fast`.

**Behavior** — single-select, fully controlled; the component holds no state. The `-1px` margin trick is what makes the active underline sit flush on the bar's hairline rather than below it.

---

## SegmentedControl

**Purpose** — a compact pill of mutually-exclusive options; the inline single-select toggle.

**Tones** — `accent` (default) or `data`; sets the active segment's fill color.

**Sizes** — `sm`, `md` (default); sets segment height, padding, and font size.

**Props**

| Prop | Type / values | Meaning |
|------|---------------|---------|
| `options` | `SegOption<T>[]` | the segments; each `{ value, label, icon? }` |
| `value` | `T` | the selected value |
| `onChange` | `(value: T) => void` | fired with the clicked segment's value |
| `size` | `sm \| md` | segment dimensions; defaults to `md` |
| `tone` | `accent \| data` | active-segment color; defaults to `accent` |
| `className` | `string` | extra class on the group |

`SegOption<T>` is `{ value: T; label: string; icon?: IconName }`. Segment icons render at 13px (`sm`) or 15px (`md`).

**States**

- Normal segment — `--text-dim`.
- Hover (inactive) — text `--text-bright` over `--bg-hover`.
- Active — text `--accent-contrast` (near-black) on the tone's solid fill (`--accent` or `--data`), plus a 12px glow in the matching `*-glow`. Active hover brightens the fill to the tone's `-bright`.

**Anatomy** — an inline flex group with 3px inner padding, 2px gaps, `--bg-inset` background, hairline border, `--radius-md`. Each segment is a flex button, `--radius-sm`, `--ls-wide`, no-wrap, color/background transitioning on `--dur-fast`. The group is the inset well; the active segment is the raised, glowing chip inside it.

**Behavior** — the active-segment glow is the signature: a solid tone fill plus a 12px `--accent-glow`/`--data-glow` bloom, so the selected segment reads as lit hardware against the dark well. Single-select, controlled, no internal state.

---

## Tooltip

**Purpose** — a hover/focus bubble of helper text anchored to a wrapped element.

**Sides** — `top` (default), `bottom`, `left`, `right`; sets which edge the bubble appears on and the slide-in direction.

**Props**

| Prop | Type / values | Meaning |
|------|---------------|---------|
| `content` | `ReactNode` | the bubble's text/content |
| `children` | `ReactNode` | the trigger element the tooltip wraps |
| `side` | `top \| bottom \| left \| right` | placement; defaults to `top` |

**States** — hidden by default (opacity 0, `visibility: hidden`, offset 2px toward the trigger). On wrap hover or `:focus-visible`, the bubble fades in and slides to its resting offset over `--dur-fast`. The wrap is `tabIndex={0}` so keyboard focus also shows it.

**Anatomy** — a relative inline wrapper around the trigger, plus an absolutely-positioned bubble at z-index `--z-tooltip`. The bubble is `--bg-panel-raised` with a `--line-strong` border, `--radius-sm`, `--shadow-pop`, text `--text` at `--fs-xs`, normal-case (it explicitly resets `text-transform`), max-width 240px, `pointer-events: none`. Each side positions the bubble `calc(100% + 8px)` off the matching edge and centers it on the cross axis.

**Behavior** — pure CSS hover/focus, no JS, no delay, no edge-clamping. The source notes this is fine for the prototype but the C++ side is expected to have a real tooltip manager with a hover delay and viewport clamping; this primitive is the visual contract for the bubble, not the trigger logic.

---

## Divider

**Purpose** — a horizontal rule, optionally with a centered stencil label.

**Props**

| Prop | Type / values | Meaning |
|------|---------------|---------|
| `label` | `string` | optional centered caption; without it, a plain rule |
| `className` | `string` | extra class |

**States** — non-interactive.

**Anatomy** — with no label, a single `<hr>` with a `--line-hairline` top border, full width. With a label, a flex row: a flexing line segment, the label, another flexing line segment, gap `--space-3`. The label is `--font-mono`, `--fs-2xs`, `--ls-wider`, uppercased, `--text-faint`; the two segments are hairline-bordered flex-1 rules that fill the space on either side.

**Behavior** — presence of `label` is the only branch: labeled renders the three-part flex row, unlabeled renders a bare rule.

---

## KeyCap

**Purpose** — a keyboard-key glyph for hints and shortcut legends.

**Props**

| Prop | Type / values | Meaning |
|------|---------------|---------|
| `children` | `string` | the key text (e.g. `"ESC"`, `"W"`, `"CTRL"`) |

The only prop is `children`, typed as `string`.

**States** — static.

**Anatomy** — a `<kbd>`, min-width 18px, 18px tall, padded 5px horizontal, `--font-mono`, `--fs-2xs`, weight 600, `--text-dim`. Background `--bg-inset`, `--bw` `--line-edge` border, `--radius-sm`. The bottom border is doubled to 2px, which gives the faint extruded-key look without a full bevel.

**Behavior** — none; it's a pure label. The min-width keeps single-character caps ("W") from collapsing narrower than multi-character ones ("ESC"), so a row of mixed caps stays visually even.

---

## Related

- [design-language.md](../design-language.md) — the look and feel these primitives serve
- [tokens.md](./tokens.md) / [tokens.json](./tokens.json) — the values every token here names
- [icons.md](./icons.md) / `icons.json` — the full glyph set
- [screens/INDEX.md](../screens/INDEX.md) — how screens compose these primitives
