# UI Layout System

`LayoutContainer` (libs/ui/layout/) is the auto-layout engine: a flexbox-like
stack container that positions and sizes its children. One container type, one
code path; there is no separate VStack/HStack.

```cpp
auto layout = UI::LayoutContainer(UI::LayoutContainer::Args{
    .position = {50, 50},
    .size = {240, 0},                          // fixed width, hug height
    .direction = UI::Direction::Vertical,
    .gap = 8,
    .padding = UI::Insets{16},
    .distribution = UI::Distribution::SpaceBetween,
    .crossAlign = UI::CrossAlign::Stretch,
});
layout.addChild(UI::Text(...));
layout.addChild(UI::Button(...));
```

## Config

- **Direction** — `Vertical` | `Horizontal`. The stacking axis is the main
  axis; the other is the cross axis.
- **gap** — fixed spacing between adjacent children.
- **padding** — per-side container insets (`Insets{top, right, bottom, left}`,
  or `Insets{uniform}`).
- **distribution** — main-axis placement: `Start`, `Center`, `End`,
  `SpaceBetween`, `SpaceAround`, `SpaceEvenly`. The Space* modes distribute
  leftover space and stack on top of the fixed gap.
- **crossAlign** — cross-axis placement: `Start`, `Center`, `End`, `Stretch`.

## Per-child sizing

Every `IComponent` carries `widthMode`, `heightMode` (`SizeMode`), and
`fillWeight`:

- **Fixed** — the explicit size is authoritative; the engine never resizes it.
  Default for plain components.
- **Hug** — sizes to content. `Text` defaults to Hug and measures itself
  (wrap-aware); a Hug container measures its children. Stretched by
  `CrossAlign::Stretch`.
- **Fill** — the parent assigns the size. Main-axis Fill children share the
  leftover space (after Fixed/Hug children, padding, and gaps) proportionally
  by `fillWeight`, as exact float shares. A cross-axis Fill child stretches
  like `Stretch`, regardless of the container's crossAlign.

A `LayoutContainer` constructed with a zero size axis is Hug on that axis;
non-zero is Fixed. Set `widthMode`/`heightMode = Fill` on the container itself
(before adding it to its parent) to make it share leftover space.

## Layout passes

`computeLayout()` runs lazily on render when dirty, in three ordered passes:

1. **Cross pass** — content box = size − padding. `Stretch` (for non-Fixed
   children) and cross-axis Fill children receive
   `setLayoutSize(contentCross − childMargin*2)` on that axis. For `Text` a
   parent-assigned width becomes the wrap width, so the next pass measures the
   wrapped height.
2. **Main pass** — Fixed/Hug children are measured via getWidth/getHeight;
   leftover = contentMain − sum − gap×(n−1) goes to Fill children by
   fillWeight.
3. **Position pass** — distribution offsets on the main axis, cross alignment
   on the cross axis, then nested `LayoutContainer` children are re-laid-out
   via `child->layout(assignedBounds)`. Other ILayer children only get
   `setPosition`/`setLayoutSize`; their `layout()` is not called (components
   like ScrollContainer manage their own coordinate space).

## Semantics that trip people up

- **Reported size is the margin box.** `getWidth()`/`getHeight()` return
  content + margin×2 for every component, containers included. An explicit
  container size is the *content* size, so `{100, 50}` with margin 10 reports
  120×70 to its parent.
- **`layout(bounds)` is a final-rect assignment.** It adopts position and
  size (content = bounds − margin×2). The engine passes Fixed children their
  own measured size back, so Fixed is never overridden by a container parent —
  but an outside caller of `layout()` does override. Don't call it with loose
  "available space" rects.
- **No negative offsets.** On overflow, distribution and alignment degrade to
  Start and children overflow past the end edge.
- **Hug main axis has no leftover.** Fill children in a Hug axis are measured
  at their intrinsic size; distribution is inert.
- **Invalidation is manual after content mutation.** There are no parent
  back-pointers (v1): after changing a child's text, size, or visibility,
  call `invalidateLayout()` on the owning container. Adding children and the
  engine's own assignments invalidate automatically.

## Verification

The `layout` scene in ui-sandbox exercises every feature and doubles as the
lint fixture: pull `/api/ui/tree` for resolved bounds and `/api/ui/lint` for
overlap/containment/viewport checks after layout changes. Unit coverage lives
in libs/ui/layout/LayoutContainer.test.cpp.

## Related Documentation

- [architecture.md](./architecture.md) - Component hierarchy
- [clipping.md](./clipping.md) - Clipping and scrolling
- [data-binding.md](./data-binding.md) - ViewModel pattern
