# Design notes

- The host decides the outcome and replicates it once; clients only display it.
- The timer counts from match start (t=0), so the Builders' 20-second head
  start is included. Show the remaining time in the HUD.
- A Builder's towers collapse with the existing demolition animation; towers
  under construction and foundations are removed too.
- Garrisoned Builders (if `builder-garrison` lands) count as alive.

## Decisions (Dougie, 2026-10-28)

- The 45-minute timer pauses while the match moves to a new host (Hunter
  takeover / authority succession) and resumes once it is running again.
- A Builder who disconnects and does not reconnect counts as dead for "all
  Builders dead", but their towers stay and keep firing. Mid-match reconnect
  does not exist today (a disconnected Builder's units and towers simply carry
  on), so in practice a disconnect counts as dead immediately; if rejoining is
  added later, they count as dead only once their rejoin window has passed.
- A Builder killed in play loses all their towers.

## Still to decide

- Does the Hunter tower keep firing after the result?
