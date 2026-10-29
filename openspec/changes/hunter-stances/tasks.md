# Tasks

- [x] 1.1 Add `HunterStance` and the authoritative per-horse stance map with Hunter-only validated `set_hunter_stance`; default Observe; clear on death.
- [x] 1.2 Share tower-first target acquisition between AttackMove and stances, with radius and optional anchor restriction.
- [x] 1.3 Start Aggressive (8-tile sight, no leash) and Defensive (4-tile range, 6-tile leash, return to anchor) engagements for idle horses without orders.
- [x] 1.4 Hand an arrived AttackMove over to a non-Observe stance; keep Observe behaviour unchanged.
- [x] 1.5 Add the Patrol order with all-or-nothing validation and endpoint swapping; engage en route with AttackMove rules.
- [x] 1.6 Add SET_STANCE and PATROL simulation commands and network command kinds, with the stance intent field and validation.
- [x] 1.7 Replicate each horse's stance to its owner only; include it in world deltas and the local presentation snapshot; bump the protocol major version.
- [x] 1.8 Bind Z/X/C stances and P-then-click Patrol for local and guest Hunters; show stance and hotkeys in the HUD.
- [x] 1.9 Test stance defaults, rejection of foreign and Builder stance commands, Aggressive and Defensive acquisition, leash and return, hand-over, Patrol looping and engagement, and stance wire round trips and privacy.
- [x] 1.10 Verify interactively in solo, then on two processes with the Hunter as a guest.
- [x] 1.11a Document the stances, Patrol and protocol v14 in `assets/fvd/README.md`.
- [x] 1.11b Update the Hunter rule in `openspec/config.yaml` and `openspec/ROADMAP.md` after acceptance.

## Patrol/stance regression (manual acceptance 2026-10-28)

- [x] 1.12 Decouple Hunter stance behaviour from the Patrol order. A horse on Patrol obeys its selected Observe/Aggressive/Defensive stance **the same as when stationary** while retaining its patrol route. **User acceptance, 2026-10-29:** the corrected Patrol/stance interactions pass real gameplay testing.

  Manual regression (repeat each stance first stationary, then while patrolling between two waypoints):
  1. Observe (C): no autonomous target acquisition or attack; a patrol continues without engaging unless given a separate explicit combat order.
  2. Aggressive (Z): acquire eligible visible enemies, favour Towers, engage without a leash, and resume the patrol route after the engagement when still valid.
  3. Defensive (X): acquire eligible enemies within 4 tiles, respect the 6-tile leash from the applicable patrol/engagement anchor, disengage and return when exceeded, then continue patrol. Confirm the exact anchor policy is deterministic and matches stationary Defensive semantics.
  4. Switch Z/X/C during an active patrol and repeat near Towers and Builders. Verify stance changes take effect immediately rather than being overridden by the Patrol order, and replicate correctly to the guest Hunter.
  5. Check that AttackMove behaviour is unchanged and that Patrol waypoint switching remains reliable after each encounter.

  **Pass:** with otherwise identical enemy arrangements, the stance determines acquisition, engagement and disengagement in both stationary and patrolling cases; Patrol determines only the route, not a forced aggression policy.

  **Fix (Linux developer):** Patrol now only sets the route; the stance decides engagement. Observe never engages. Defensive engages within 4 tiles, gives up 6 tiles (the stationary leash) from where it left the route, returns there and resumes. Aggressive chases with no leash while the target stays in its sight, and resumes the patrol after the target dies or escapes. Aggressive acquisition is capped at the horse's 7-tile sight by fog of war, not 8. Covered by `fvd_hunter_patrol_stances` and the accepted graphical rerun.
