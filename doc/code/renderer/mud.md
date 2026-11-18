# FVD battlefield wear and muddy terrain

FVD gradually scars the map as units walk over it and as Builders construct
and rebuild towers. The damage is permanent for the duration of the match.
This is a purely visual effect: movement speeds, terrain passability and
fog-of-war visibility do not change.

## Authoritative wear

The host maintains **66 exact wear units** per terrain tile. It
projects these to the network/renderer byte map (0–255) in
`FVD::terrain_wear`. Both arrays use fog's index order:
`ne * map_size + se`.

**One Hunter pass contributes 1 unit**; **one Builder pass contributes
2 units**. Thus precisely **66 Hunter passes** or **33 Builder passes**
bring an untouched tile to maximum wear. A moving escape cart counts as a
Builder pass. Different unit types can contribute to the same tile.

A *pass* means a moving unit entering/traversing a tile. The simulation
tracks actual tile-boundary crossings rather than sampling every fraction
of a tile: standing still, repeatedly updating a position within the
same tile, and small frame-to-frame movements do **not** add extra passes.
Leaving and re-entering counts as another pass. Unexpected jumps of 8
tiles or more are ignored, to prevent teleport trails.

Each successfully started tower construction contributes **2 units** to
each tile of its footprint, like one Builder traversal. Repeated builds
and demolition eventually churn the area, but not nearly as quickly
as before. Demolition never restores the grass.

All values saturate at 66 units, and the terrain shader now reserves
full mud coverage and puddles for the highest wear levels.

The tuning constants are in `FVD`'s terrain wear definitions in
`libopenage/gamestate/fvd.h`, with traversal accounting in
`fvd_combat.cpp` and tower wear in `fvd.cpp`.

## Multiplayer

Terrain wear is public, host-authored map state included in FVD
`WorldState`. A full snapshot stores a densely packed byte per tile,
including pristine tiles. Normal `WorldDelta` messages contain only
the changed nonzero tile intensities. Deltas must only increase wear;
rollback or missing records are rejected.

This allows new clients and recovery snapshots to reproduce the same
battlefield without rebuilding visual history from unit movements.
FVD protocol major version 26 includes the wear payload.

## Appearance

The terrain stage uploads the byte map as a texture only when the host's
captured wear levels change. The terrain fragment shader interpolates
between neighbouring tiles to soften paths, combines static multi-scale
noise with irregular soil colour, and adds subtle animated reflections
to the wettest ruts.

Wear is composited before fog darkness, so undiscovered regions remain
hidden. The existing constant rainfall effect makes muddy areas feel
particularly wet.

For tuning, see `assets/shaders/terrain.frag.glsl` and
`libopenage/gamestate/fvd_combat.cpp`.

## Verification

Run the FVD gamestate, network message and world-delta tests before
shipping. Inspect the effect in a running native game at several zoom
levels, including network sessions, reconstructed snapshots and new match
resets. The terrain is cosmetic; no audio, pathing or collision changes
are introduced.
