# FVD battlefield wear and muddy terrain

FVD gradually scars the map as units walk over it and as Builders construct
and rebuild towers. The damage is permanent for the duration of the match.
This is a purely visual effect: movement speeds, terrain passability and
fog-of-war visibility do not change.

## Authoritative wear

The host maintains a byte (0 to 255) for every terrain tile in
`FVD::terrain_wear`. This array uses the same indexing as fog:
`ne * map_size + se`.

On each 0.2-second FVD combat simulation event, walking Builders, Hunter
combat units and moving escape carts leave an additional 7 wear points
every 0.55 world tile travelled. Stationary units do not leave footprints.
The system samples only movement actually made; it does not generate wear
along hypothetical orders. Unexpected jumps of 8 tiles or more are ignored
to avoid trails across the map from teleports or corrections.

Each successfully started tower construction adds 38 points to every
tile of its footprint. Demolition does not restore grass. Consequently,
repeatedly rebuilding fortifications creates deep muddy patches even if
no units travel across them. All intensities saturate at 255.

These constants are in `FVD::record_terrain_tracks` and
`FVD::advance_build` respectively.

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
