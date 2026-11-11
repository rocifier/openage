# Proposal

## Why

Players can't see the whole map at once: where the Hunter's horses are, where
teammates are walling, or which areas are still unexplored.

## What Changes

- A minimap at the **bottom left**, drawn as an AoE2-style diamond matching
  the camera. The build panel moves to its right, touching it.
- It shows the player's own **fog of war** (black unexplored, dark explored,
  bright visible), **buildings** in their owner's colour (foundations darker,
  fog-remembered enemy buildings darker still), and **units** (Builders and
  horses) as bright dots in their owner's colour.
- A white box shows what the main camera currently sees.
- Left-click or drag on the minimap moves the camera there.
- Client-only: it uses the world each player already receives, so it shows
  exactly what that player may see. No protocol change.

## Capabilities

### New Capabilities

- `fvd-minimap`: map overview and camera navigation.

## Status

Implemented (Dougie, 2026-11-03). Flares appear on the minimap through
`builder-flare`; manual acceptance passed 2026-11-11.
