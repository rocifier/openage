# Proposal

## Why

Builders have no way to hide from horses. Garrisoning lets a Builder shelter in
their own towers and hop along a wall of towers.

## What Changes

- With a Builder selected, **T** enables garrison mode; clicking one of your
  finished towers sends the Builder there and puts him inside.
- A garrisoned Builder is hidden and cannot be targeted.
- A shortcut key (**G**) ungarrisons the Builder. With no ungarrison
  point set, he steps straight out onto a free tile next to the tower and
  stays there, even if horses are nearby.
- Optionally, a tower gets an **ungarrison point**, set like the Hunter's
  rally point: select the tower and right-click. It uses the same red arrow
  sprite as the rally point, visible only to the tower's owner. When one is
  set, the Builder walks there on ungarrisoning.
- If the ungarrison point is on one of your finished towers **adjacent** to the
  current one, ungarrisoning moves the Builder straight into that tower instead
  (hopping between towers).

## Capabilities

### New Capabilities

- `fvd-builder-garrison`: garrison, ungarrison point, hopping.

## Status

Proposed (Dougie, 2026-10-28). Not implemented.
