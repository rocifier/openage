# Tasks

- [x] 1.1 Implement match-relative grace-period scheduling: no spawns before t=20s; one horse at t=20s and every second thereafter.
- [x] 1.2 Keep new Hunter horses free of autonomous orders and resolve spawn-area overlaps; test timing, crowd separation and blocked centre headlessly.
- [x] 1.3 Add human Hunter owned-horse selection, including group selection, and ordinary Move orders.
- [x] 1.4 Add AttackMove orders through the existing controller, command queue, pathfinding and simulation; fight eligible enemies en route, then resume travel. **User acceptance, 2026-10-29:** Hunter AttackMove passes real gameplay testing.
- [x] 1.5 Enforce Hunter-only horse command ownership in solo and network role assignments; reject Builder-issued Hunter orders. **User acceptance, 2026-10-29:** only the Hunter can issue horse AttackMove commands.
- [x] 1.6 Add horse attack/damage behavior and tower targeting, logical arrow impacts, visual flight trails, damage and deaths for completed towers (remote visuals tracked by synchronized-match).
- [x] 1.7 Add idempotent lethal-damage credit to the owner of a killing tower.
- [x] 1.8 Expose only the current player's kill total through the local HUD (network delivery follows synchronized-match work).
- [x] 1.9 Test exact t=19/20/21/22-second spawning, idle groups, command handling, resumed AttackMove and simultaneous lethal hit attribution. The Windows asset-backed test run and real AttackMove acceptance pass.
- [x] 1.10 Integrate the later Hunter rally-point change through the authoritative order pipeline and retain idle spawning when no rally point is set. Detailed guest-Hunter acceptance remains tracked by `hunter-rally-point`.

- [x] Add a local Hunter focus-attack command for deliberately targeting a Builder despite a nearby tower, with ownership validation and cancellation on target death. Remote command forwarding is part of the networking work.
- [x] 1.11 Show the shared simulation-time countdown to the first Hunter horse in the top-centre HUD.
