# Design explorations (future-state)

Parking lot for **future-state** design directions that we want on record but are
**not** part of the committed [build order](../README.md#3-design-elements-build-order).
Unlike a [design element](../README.md) (`de-*.md`) — a self-contained slice we build
and prove now — an *exploration* is a sketch of a direction we may take later. It does
**not** get an FFL-traced isolation test or a slot in the status table until it's
promoted to a real design element.

Use these to capture rationale, trade-offs, and open questions while they're fresh, so
the eventual element doesn't start from a blank page.

| Exploration | Area | Status |
|-------------|------|--------|
| [Magnetic mounting](mounting-magnetic.md) | brake_light enclosure / mount | 💭 exploring |

Legend: 💭 exploring · ⏳ ready to promote to a `de-*` element.

> These are **ideas under consideration**, not commitments. The current baseline
> mount in [`hardware.md`](../../hardware.md) and the hard safety rules in
> [`safety-regulatory.md`](../../safety-regulatory.md) still govern anything we
> actually build until an exploration is promoted.
