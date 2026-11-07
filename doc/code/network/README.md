# FVD networking

FVD multiplayer uses one host-authoritative simulation.
Guests can connect directly to that host on a reachable network or both sides can make outbound TCP connections through a relay.
The host participant submits commands through the same validation boundary but does not need a loopback socket.

See [the engine architecture](../architecture.md) for the subsystem boundaries and [the synchronized-match design](../../../openspec/changes/synchronized-match/design.md) for the gameplay authority model.

## Startup relay lobby

Launching the FVD game normally opens a Qt Quick multiplayer menu, leaving the
simulation clock paused at zero. `run game --solo` retains the existing
local gameplay and `run test` uses the existing test runner.

The menu asks for a relay DNS/address, port, player name and optional private-alpha token.
A host supplies a game name and creates an advertised room whose nonzero ID comes from the relay.
A guest refreshes the room list, enters one of those IDs and joins it.
It uses the relay directory admission and existing session handshake, then sends
`PARTICIPANT_SNAPSHOT` from the host and `START_MATCH` after the host clicks
Start Game. The Hunter/Builder draw occurs at Start; the same validated
`START_MATCH` drives each client's five-second role reveal. The reveal shows
participant names, every role, and a prominent local Hunter/Builder designation.
It is measured using each client's steady (wall) clock, so match synchronization
and the paused simulation do not shorten or restart the five-second display.
The roster and assignments come from validated network messages, not local
placeholder role calculations. After the lottery the
original accepted session (session ID and frame sequence numbers unchanged)
is rebound exactly once from its provisional slot to the guest's **final**
slot 1 or 2. Both peers validate that the assignments match the admitted
roster, not merely the advertised host ID.

The simulation thread then captures an authoritative `WORLD_SNAPSHOT` for
that final guest slot at simulation time zero. The host sends it over the
already-authenticated relay connection; the guest validates it using a
`ReplicationTracker` and returns `MATCH_READY` (the 16-byte player ID and
world revision acknowledgement). Only after matching that acknowledgement
does the host indicate **bootstrap synchronized**. Cancellation invalidates
outstanding simulation snapshot requests to prevent stale cross-match results. Failed admission and
handshakes report their errors in the menu; socket work is on a separate thread.
Gameplay inputs are suppressed behind the overlay.

**Current limitations:** The screen can host **2–8 people**, selected using
"Maximum players". The default remains two, preserving the existing tested
single-guest path. When hosting a larger room, `RelayLobbyClient` uses
`RelayMultiHostLobby` and sends each guest its own initial snapshot and
live world-delta stream. All guest orders enter one authoritative simulation,
and command replies are returned only to their authenticated sender.
The relay address must still be known because there is no embedded relay
server, global service directory or hardcoded public relay.
On a fresh room-list request, the relay drops advertised rooms whose host TCP
connection has closed, and refuses a join if its host has already disconnected.
The UI's room list is refreshed by **Refresh Games**, not pushed in real time.
An empty request retries briefly so a room being published concurrently with the refresh is included.
Role assignment releases lobby text focus, and persisted numeric fields are validated before reuse.
Session IDs now survive the final role rebinding, and the initial world
snapshot/acknowledgement runs through that same relay connection. The host can
now queue complete recipient-specific state captures; the relay worker computes
and sends versioned `WORLD_DELTA` frames based on the last **transmitted**
state, and the guest validates/applies them using `ReplicationTracker`. After the five-second reveal, the host resumes its simulation and the
presenter requests simulation-thread captures every 50 ms by default (20 updates per second).
The host can tune this cadence with `game --network-update-interval-ms MS`, where `MS` must be positive; this is replication sampling, not a simulation tick rate.
The guest draws replicated Builder/Horse/Tower/Foundation/TowerDemolition sprite proxies,
uses only its assigned faction, and sends basic Move, AttackMove and BuildTower
intents to the host for simulation-side validation. Host decisions return as
`COMMAND_RESULT` messages. The full-screen role dialog disappears after
the reveal; F10 disconnects and rebuilds the world for another room.
The multi-guest host/guest path is implemented but still requires its first
Windows GUI and network smoke tests. Guest focus attacks, foundation
prioritization, queued tower orders and snapshot recovery are implemented. Live authenticated
PING/PONG heartbeats detect silent disconnections after 15 seconds, and frame
writes are also bounded to 15 seconds. Complete HUD parity, authenticated
resume after TCP loss, and Internet transport security remain unfinished.
For a multi-guest room, the game-simulation thread removes starting Builders
for never-admitted slots before bootstrap and marks those slots disconnected
in every recipient's authoritative world state. Builders who join and later
disconnect retain their existing units and towers. The legacy two-player
hosting path is left unchanged for regression stability.
It does not launch two independent local worlds or misrepresent them as
synchronized multiplayer.
The provisional asset handshake digest must be replaced by a manifest digest
before Internet deployment; the existing TCP relay has no encryption.

### Manual two-player relay check

Run the persistent relay and the two game clients in three terminals.

```sh
bin/run relay
```

Start `bin/run game` twice.
Use address `127.0.0.1` and port `40000` in both lobby screens.
Choose different player names.
On the first client, enter a game name and select **Host Game**.
On the second client, select **Refresh Games** and then **Join Room** to use the first result, or enter another displayed room ID before joining.
The host selects **Start Game** after both clients appear. The authoritative
Hunter/Builder draw is revealed to both participants for five seconds; both
screens then reach synchronized bootstrap at world revision 1. After
the five-second reveal the dialog closes and the host-authoritative simulation
starts. Both players should see the same units; the first Hunter horses begin
spawning 20 simulation seconds later. Select the assigned Builder to move or
build (B then click), or as Hunter select horses and press A then click to
attack-move. Press F10 to disconnect and allow the simulator and renderer
to recreate a fresh time-zero match world before hosting the next room.
The rematch lifecycle still requires physical Windows verification.
The relay remains available for later rooms after either client disconnects.

For a private alpha, start the relay with one repeatable `--player-token TOKEN_64_HEX` option per authorized player.
Every host, refresh and join request must then include one listed token.
The host and guest of a room must use different tokens.

## Pre-match lobby core

`network::HostLobby` now owns the pre-start participant roster independently
from the game-state player slots. The host can add/remove named guests (at most
eight humans), publish a complete `ParticipantSnapshot`, and start exactly
once when at least two people are present. Invalid, repeated and late joins,
unknown or host departures, and duplicate participant identities are rejected.

At Start, the host chooses one Hunter using a uniformly selected ticket in
`[0, 2*N-1)`: the host has one ticket and every guest has two. This yields
one-third host Hunter probability for two humans and one-fifteenth for eight.
The Hunter receives gameplay slot 1, and remaining players receive distinct
random Builder slots 2..N via Fisher-Yates. The lottery can use production
randomness or an injected bounded draw for deterministic tests. Lobby identity
and slot assignment become immutable at Start.

`PARTICIPANT_SNAPSHOT` and `START_MATCH` now have validated payload
encoders/decoders. Both include host participant ID and lobby revision.
`PARTICIPANT_SNAPSHOT` includes each stable participant ID, host flag and
name; `START_MATCH` includes the final participant-to-player mapping and
Hunter/Builder role. The decoder rejects invalid sizes, reserved flags,
duplicate identities and malformed role allocations.

The two-person relay lobby broadcasts these payloads and rebinds its provisional
guest slot to the final assignment before exchanging the initial world.
`MatchConnectionState::restrict_to_participants()` now distinguishes admitted
player slots 1..N from the eight slots created by the FVD scenario. The new
`fvd_multi_session` integration harness exercises three and eight players
with independent TCP handshakes, concurrent authenticated Builder intents,
per-recipient deltas, and a Builder departing while survivors continue.
It uses direct loopback sockets rather than the public room directory, which
now supports up to seven independently forwarded guest channels per room via
`RelayMultiHostLobby`. Its 3/8-person integration test covers roster revisions,
role assignment, private bootstrap frames, and sequenced command round trips.
The GUI selects the original two-player hosting path when its room capacity
is 2, or the new multi-host path for 3–8. The presenter captures
recipient-specific bootstraps and round-robin world states on the simulation
owner thread, allowing the relay worker to maintain an independent
`WorldDeltaBaseline` and command/result queue for every guest. The
`lobby_relay_client` integration test now also starts a three-client room,
tests individual private snapshots and order replies, and verifies a Builder
departure followed by another successful surviving player's command.

## Transport

The transport uses TCP through Winsock on Windows and POSIX/BSD sockets on Linux and other supported Unix-like systems.
TCP matches the lobby, command and state-replication traffic and provides the required ordered reliable stream without another dependency.
The socket implementation belongs in `libopenage/network/` and does not depend on the presenter, renderer or Qt Quick.
Blocking receive/send retries wait for readiness through `WSAPoll()` on Windows and `poll()` on POSIX with a bounded 250ms interval. This avoids `select()`/`FD_SET()` and the POSIX `FD_SETSIZE` descriptor-index ceiling, which commonly caused undefined behavior when a busy Linux test process opened socket descriptor 1024 or higher.
The `transport_high_fd` regression opens sufficient file descriptors to exercise TCP receiving above that threshold when the OS soft file limit permits it; it skips on systems with too-low limits.

`network::TcpListener` binds an endpoint and accepts `network::TcpConnection` objects.
Connections send and receive complete `Frame` values while handling partial TCP reads and writes internally.
Connected and accepted sockets use `TCP_NODELAY` so small commands, replies and world updates do not wait for TCP packet coalescing.
Live host and guest workers poll pending traffic at 5 ms intervals instead of adding a 20 ms idle sleep at each endpoint.
One thread may read while one thread writes the same connection; `close()` safely interrupts either direction.
An orderly peer shutdown returns no frame; socket failures and truncated frames raise an `Error`.

Each accepted guest has one connection and one host-generated, nonzero 64-bit session ID.
The host binds that connection to the lobby participant and assigned player; messages never carry a client-selected player identity.
The session ID detects stale messages and routing mistakes but is not a security credential.
The transport does not yet encrypt traffic or authenticate the authoritative host.

## Relay rooms and pairing

An Internet relay gives every participant an outbound connection, so the game does not depend on router port forwarding or NAT traversal.
The authoritative player host opens one relay connection for each remote participant and every guest opens its corresponding connection.
After pairing, the host runs the existing server handshake over that outbound socket and the guest runs the existing client handshake.
The relay forwards complete FVD frames in both directions and does not create a simulation, assign a session ID or interpret gameplay payloads.

`network::RelayRoomDirectory` owns a bounded set of host-created rooms.
`RelayCreateRoom` advertises a room name and host name and returns a relay-generated room ID.
`RelayListRooms` returns the live rooms in ID order. Legacy two-peer hosts
publish only one slot, so the first join consumes their room. Multi-guest
hosts use `RelayAttachRoom` to reserve further channels (provisional IDs
3..8), and each `RelayJoinRoom` consumes exactly one available host socket,
leaving the room advertised until the final slot is paired. The relay sends
`RelayRoomAttached` after validating an additional host channel.
Each host/guest pair retains an independent TCP session and frame sequence.
Public rooms generate a nonzero private creator token for host-channel
attachment, rather than using the public zero guest token.
The relay sends `RelayReady` with the room ID and provisional guest player ID 2 after both sockets are paired.
Normal `ClientHello` and gameplay frames follow `RelayReady` unchanged.
Room listing sends a pre-session heartbeat to waiting hosts so cleanly disconnected rooms can be removed instead of remaining advertised.

An empty relay authorization list enables public room creation, listing and joining.
A nonempty list restricts all three operations to repeatable 256-bit player bearer tokens supplied through `--player-token`.
The same private token cannot represent both sides of one room.
Room IDs are discovery identifiers rather than credentials.
Tokens are visible on the current unencrypted TCP connection, so an Internet deployment must protect the relay transport before treating them as a security boundary.

The older low-level provisioned-pair API remains available to deterministic transport tests.
It provisions a nonzero match ID, a nonzero guest player ID and two distinct single-use 256-bit bearer tokens for each host-to-guest pair.
It consumes `RelayConnect`, rejects malformed, unauthorized or duplicate-role admissions, and sends `RelayReady` only after both sides have arrived.

`network::RelayPairRegistry` holds a bounded set of lobby-provisioned pairs and safely admits connections from concurrent service workers.
A pair leaves the registry when both roles authenticate, while lobby cancellation removes a pending pair and closes any role that already connected.
Each completed pair is passed to its own `network::forward_relay_pair`, which closes both connections when either side disconnects without affecting other pairs.
`network::accept_relay_pair` remains the single-pair convenience entry point used by deterministic tests.
Direct TCP remains available to deterministic transport tests.

## Session and gameplay boundary

`network::ConnectionSession` is created after a successful handshake with a host-assigned nonzero session ID and player ID.
The binding is immutable for the connection lifetime.
The connection thread owns the session object and uses it to validate every inbound session ID, negotiated protocol version and contiguous sequence number.
The same object creates outbound frames with the negotiated version, correct session ID and next sequence number.
After validation and payload decoding, connection threads place authenticated commands into a bounded `CommandMailbox`.
The simulation thread drains this mailbox in FIFO arrival order and supplies its authoritative time when scheduling commands.
A full mailbox is explicit backpressure: the connection handler must reject the command or disconnect the peer rather than dropping it silently.
Simulation output is placed into one bounded `OutboundMailbox` per accepted connection.
Its values are typed command results, snapshots, deltas or disconnect notifications; only the owning connection thread converts them to sequenced frames and writes the socket.
An outbound mailbox that remains full identifies a client that cannot keep up, so the host disconnects that client instead of silently dropping authoritative state.
Connection threads publish closed sessions through `ConnectionEventMailbox` for authoritative processing.
`MatchDisconnectCoordinator` binds each remote session to its own
`OutboundMailbox` and processes those closures on the simulation thread
**before** draining the next batch of authenticated commands. It checks outbound
capacity for both messages before changing any authoritative connection state.
For each unique closure, it permanently invalidates the session, advances
the world revision once, queues one `ParticipantDisconnected` notification to
each surviving remote, then queues an incremental `WorldDelta` carrying
that participant's `connected = false` flag. Identical repeat close events do not
advance the revision twice. An unknown or mismatched session closure, or
insufficient outbound capacity, is fatal to the host match rather than silently
dropping replication. Outbound mailboxes remain owned by connection handlers
and must outlive their registration with the coordinator.

Gameplay payloads never contain a claimed player ID.
The network boundary passes the `AuthenticatedCommand` drained from the mailbox to `submit_authenticated_command`.
The bridge schedules work on the authoritative event loop and returns a `CommandResult` decision.
When draining commands from `CommandMailbox`, use the overload of
`submit_authenticated_command` taking `MatchConnectionState`:
it re-checks the **current** session binding before scheduling the event.
Previously accepted commands already submitted to the simulation are not
rolled back, but commands still in the inbound mailbox when a disconnect is
applied are rejected with a negative callback and do not mutate gameplay.
The connection handler reports rejection with the original command ID and
the current authoritative revision; it must never optimistically apply
the rejected command.
Gameplay code can add command behavior and replicated records without reading sockets, framing bytes or maintaining connection counters.
Network code can change connection management without changing horse, projectile, damage or building simulation.

## Relay lobby developer test

Run the public relay service separately:

```sh
bin/run relay
```

Launch two copies of `bin/run game` and use relay address `127.0.0.1` and port `40000`.
On the first client choose **Host Game**.
On the second choose **Refresh Games**, enter the listed room ID and choose **Join Room**.
Click **Start Game** on the host. Both clients reveal the same Hunter/Builder
allocation for five seconds, then reach **bootstrap** after transferring and
acknowledging the initial world.

Private-alpha mode uses repeatable authorized player tokens:

```sh
bin/run relay \
  --player-token HOST_TOKEN_64_HEX \
  --player-token GUEST_TOKEN_64_HEX
```

Enter the corresponding token on each client before hosting, refreshing or joining.

Authority succession safety checks:
`bin/run test openage::network::tests::authority_succession` and
`bin/run test openage::network::tests::rehost_ticket`. This verifies
Hunter-only termination, monotonic epochs, stale takeover rejection, and the
all-participant fresh-session barrier required for a pre-game role-preserving
Hunter rehost. The `relay_multi_host_lobby` test also exercises
`start_fixed()` for 3 and 8 players.
These are authority **foundation** checks; they do not validate a live
Builder-host disconnect or real simulation restoration.

Focused automated checks: `bin/run test openage::network::tests::lobby_relay_client`, `bin/run test openage::network::tests::lobby_capacity`, `bin/run test openage::network::tests::match_disconnect_coordinator`, and `bin/run test openage::network::tests::fvd_multi_session` (requires FVD assets). The `lobby_capacity` regression exercises four and eight separate live client networking workers, both original-room and forced Hunter-owned starts, recipient-private snapshots, concurrent orders, Builder continuation, and terminal Hunter loss. Run the two-PC and visual/gameplay acceptance checks separately using [WINDOWS_MULTIPLAYER_ACCEPTANCE.md](WINDOWS_MULTIPLAYER_ACCEPTANCE.md); this automated test does not replace them. Additional multi-room checks are `bin/run test openage::network::tests::relay_service` and `bin/run test openage::network::tests::relay_multi_host_lobby`; the latter covers a single public room with 3 and 8 participants, shared roster/roles, recipient-bound bootstrap frames and authenticated command/result round trips. The relay test exercises command-generation rejection after close/reconnect, several simultaneous queued/targeted orders, and host acknowledgements. The coordinator test covers Builder survival and terminal Hunter departure with a final replicated notice.
Relay directory and access-control check: `bin/run test openage::network::tests::relay_service`.
Local non-network gameplay: `bin/run game --solo`.

## Frame format

TCP is a byte stream, so receivers use `network::FrameParser` to handle partial and coalesced reads.
Every integer uses network byte order.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `FVDN` |
| 4 | 2 | Protocol major |
| 6 | 2 | Protocol minor |
| 8 | 2 | Message type |
| 10 | 2 | Flags, currently zero |
| 12 | 4 | Payload size |
| 16 | 8 | Session ID |
| 24 | 8 | Per-direction sequence number |
| 32 | variable | Message payload |

Payloads are limited to 1 MiB.
The handshake uses session and sequence zero.
After acceptance, each direction starts at sequence one and the receiver rejects duplicates, regressions and a session ID that does not match the connection.

The current messages are `ClientHello`, `ServerAccept`, `ServerReject`, `ParticipantSnapshot`, `StartMatch`, `CommandIntent`, `CommandResult`, `WorldSnapshot`, `WorldDelta`, `ParticipantDisconnected`, `Ping`, `Pong`, `SnapshotRequest`, `RelayConnect`, `RelayReady`, `RelayReject`, `MatchReady`, `RelayCreateRoom`, `RelayRoomCreated`, `RelayListRooms`, `RelayRoomList`, `RelayJoinRoom`, `RelayAttachRoom` and
`RelayRoomAttached` (protocol 7.3).
Payload schemas use fixed-width network-order integers, length-prefixed UTF-8 strings and length-prefixed arrays.

### Relay admission payloads

`RelayConnect` uses session and sequence zero and contains an 8-byte match ID, 8-byte guest player ID, 2-byte host (`1`) or guest (`2`) role, 2 zero flag bytes and a 32-byte role-specific token.
`RelayReady` echoes the 8-byte match ID and 8-byte guest player ID.
`RelayReject` uses the same length-prefixed, displayable reason encoding as `ServerReject`.
These frames are valid only before relay pairing and are rejected by an accepted `ConnectionSession`.

### Relay directory payloads

Every directory request uses session and sequence zero and begins with a 32-byte player token.
Public clients send an all-zero token, while a private relay requires one token from its launch-time authorization list.
`RelayCreateRoom` appends a length-prefixed room name of at most 64 bytes and a length-prefixed host name of at most 128 bytes.
`RelayRoomCreated` contains the generated 8-byte room ID.
`RelayListRooms` contains only the token.
`RelayRoomList` contains a 4-byte count followed by entries containing an 8-byte room ID, length-prefixed room name and length-prefixed host name.
`RelayJoinRoom` appends the requested 8-byte room ID to the token.
`RelayReady` identifies a directory pair with its room ID as the match ID and provisional guest player ID 2.
`RelayReject` reports malformed requests, failed authorization, capacity limits and rooms that are no longer available.
All directory frames are rejected by an accepted `ConnectionSession`.

### Command intent payload

The connection supplies player identity, so the payload cannot claim an owner.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | Client-generated command ID |
| 8 | 2 | Command kind: move `1`, attack-move `2` or build-tower `3` |
| 10 | 2 | Flags, currently zero |
| 12 | 4 | Entity count, at most 1,024 |
| 16 | `8 * count` | Entity IDs |
| `16 + 8 * count` | 8 | Target northeast coordinate |
| `24 + 8 * count` | 8 | Target southeast coordinate |
| `32 + 8 * count` | 8 | Target height coordinate |

Coordinates are signed Q16.16 fixed-point values serialized from their raw 64-bit representation.

### Command result payload

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | Client-generated command ID |
| 8 | 2 | Status: accepted `1` or rejected `2` |
| 10 | 2 | Flags, currently zero |
| 12 | 8 | Authoritative world revision |
| 20 | 8 | Authoritative simulation time as its raw signed fixed-point value |
| 28 | 4 | Rejection-reason byte length, at most 4,096 |
| 32 | variable | UTF-8 rejection reason |

### World state payload

`WorldSnapshot` carries one complete authoritative FVD state at a simulation time.
`WorldDelta` uses a **separate incremental payload**: only records changed since
the recipient's previous accepted revision, plus explicit IDs of removed records.
A recipient reconstructs its world by applying an uninterrupted sequence of
deltas to its initial snapshot, or requests a complete recovery snapshot after a gap.
The host maintains an independent baseline for every recipient, so private kill
scores and revision continuity remain recipient-specific.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | Base world revision |
| 8 | 8 | World revision carried by this message |
| 16 | 8 | Authoritative simulation time as its raw signed fixed-point value |
| 24 | 4 | Square map size in tiles |
| 28 | 8 | Recipient player ID |
| 36 | 8 | Recipient player's kill count |
| 44 | 4 | Playable player count, at most 8 |
| 48 | 4 | Entity count, at most 16,384 |
| 52 | 4 | Reserved tile count, at most 16,384 |
| 56 | 4 | Active construction count, at most 1,024 |
| 60 | 4 | Active Hunter attack-order count, at most 16,384 |
| 64 | 4 | Recent arrow launch-event count (active or within 3 seconds after impact), at most 2,048 |
| 68 | variable | Player, entity, reservation, construction, attack-order and arrow records in that order |

A player record contains an 8-byte player ID, a 2-byte Hunter or Builder role and 2 flag bytes.
Player flag bit 0 is set while that participant remains connected to the running match.
An entity record contains an 8-byte entity ID, 8-byte owner ID, a length-prefixed nyan definition of at most 256 UTF-8 bytes, three signed Q16.16 position coordinates, 2 flag bytes, 2 reserved zero bytes, signed 8-byte current and maximum health values, an unsigned 8-byte individual tower-kill count, and a 2-byte count of up to 16 future motion keys. Each motion key stores an 8-byte signed fixed-point time and three signed 8-byte position coordinates; times must be strictly increasing and later than the message's simulation time.
Health flag bit 0 is set when both health values apply; entities without a `Live` ability carry zero values and clear the flag.
Tower-kills flag bit 1 is set only for a Tower owned by the recipient; otherwise the kill-count field is zero and the flag is clear.
A reservation record contains two signed 8-byte tile coordinates and the 8-byte ID of the Builder holding the tile.
A construction record contains its 8-byte order, owner and Builder IDs, two signed 8-byte target tile coordinates, a 2-byte planned or under-construction phase, 2 zero flag bytes, an 8-byte phase deadline and the 8-byte foundation or tower entity ID.
An attack-order record contains an 8-byte horse ID, three signed Q16.16 destination coordinates, 2 flag bytes, 2 reserved zero bytes and an 8-byte target entity ID.
Attack-order flag bits 0, 1 and 2 indicate a target ID, arrival at the requested destination and an explicit focus target respectively.
An arrow record contains 8-byte arrow, owner and target IDs, three signed Q16.16 launch coordinates, three signed Q16.16 destination coordinates and signed 8-byte launch and impact times.
Completed towers remain in the entity list and have no active construction record or reserved tiles.
Killed entities disappear from the next complete state. Arrow launch events remain available for up to 3 seconds after impact, then expire without a tombstone; projectile damage is already reflected in the authoritative health/death state. Guests may play an overdue shot visually once without calculating any hit or damage.
The host builds state separately for each connection, so the payload contains only its recipient's lifetime Builder kill total and its own Towers' individual kills, never an opponent's score.
The Builder total includes historical kills from destroyed towers; individual Tower counts are displayed only when selecting that Tower.
On a guest, construct `ReplicationTracker` with the immutable player ID from the accepted session.
It rejects snapshots or deltas addressed to another player, validates every delta's
base revision and reconstructs the complete local world before publishing score
changes. Rejected, malformed or gapped deltas cannot partially modify the world.
A revision gap requests a replacement snapshot; host loss makes scores and the
reconstructed state unavailable.
Bind this tracker with `Presenter::set_fvd_replication` before the presenter starts to restrict role switching and use the replicated score in the HUD instead of the local simulation's count.
Before the initial snapshot or after host loss, the HUD shows an unavailable score rather than a guessed zero.
Protocol major version 2 introduced the complete state layout because it is incompatible with the earlier position-only payload.
Protocol major version 3 added persistent participant connection status to player records.
Protocol major version 4 adds the individual Tower kill-count field to every entity record and must not connect to version 3 peers.
Protocol major version 5 adds the distinct incremental WorldDelta payload and is
incompatible with version 4 clients.
Protocol major version 6 removes arrow tombstones: clients expire replicated
projectiles locally from their launch/impact timestamps. Version 5 peers are
incompatible with the shortened delta payload.
Protocol major version 7 adds `MATCH_READY` and strict, one-time role
rebinding after `START_MATCH`. Earlier versions are incompatible.
Protocol major version 8 extends every entity record with its future motion-key count and curve keys. Clients with the old record framing cannot connect.
Protocol minor version 1 supports optional relay admission within the same protocol major version.
Protocol minor version 2 adds relay room creation, discovery and joining.
The encoder rejects any complete payload larger than the 1 MiB frame limit.

### Incremental WorldDelta payload (introduced in protocol 5, extended in 8)

A delta has a 28-byte header, a length-delimited partial WorldState section
(encoded using the same individual record representation as a snapshot) and
six lists of removal identifiers. Its header contains:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | Delta flags: bit 0 means the recipient lifetime kill count changed |
| 2 | 2 | Reserved, zero |
| 4 | 4 | Partial WorldState section length |
| 8 | 20 | Five 4-byte removal counts, in the order below |
| 28 | variable | Partial WorldState, then removal IDs |

The partial section carries base/world revisions, simulation time, map size and
recipient identity, followed by **upserts only**. Player, entity, reservation,
construction, attack order and arrow records use their existing snapshot shapes.
The header always contains a local kill field, but clients apply it only when
delta flag bit 0 is present; otherwise the field is zero and the previous total
is retained. An empty player list is valid in a delta but not a snapshot.

Removal lists follow in this order: player IDs (u64), entity IDs (u64),
reservation coordinates (two signed i64), construction order IDs (u64),
and Hunter attack-order horse IDs (u64). Arrows have no removal list:
the client stops normal flight rendering at the supplied impact time, but
keeps a short 3-second launch record to support one-shot visual replay when
recipient updates arrive after impact. The next clock update expires the
record without any arrow tombstone.
The decoder bounds every count, requires exact payload length, and rejects
unsupported flags and malformed records. Applying a delta rejects unknown or
duplicate removals, conflicting updates/removals, noncontiguous revisions,
map or recipient mismatches and unauthorized tower score disclosures.

Snapshots remain complete and use the current version-8 entity record format;
they replace every record and reset the host and guest delta baselines. A
WorldDelta with no changes still advances the authoritative world revision
without copying every entity. Re-sending the entire world as a delta is no
longer supported.

### Snapshot request payload

`SnapshotRequest` carries the client's last applied world revision as one 8-byte unsigned integer.
It is sent once when a client receives a delta whose base does not match that revision.
The client suppresses later deltas until a complete recovery snapshot arrives.

### Participant disconnected payload

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | Nonzero player ID |
| 8 | 2 | Reason: connection lost `1`, left match `2`, protocol error `3` or host shutdown `4` |
| 10 | 2 | Flags, currently zero |

## Compatibility handshake

`ClientHello` supplies the participant name, protocol version, game build version and an FVD asset manifest digest.
The host answers with `ServerAccept` and the assigned session ID or `ServerReject` and a displayable reason.
Protocol major versions and the required asset digest must match.
A newer minor version may connect only when its advertised minimum compatible minor includes the peer's version.
The host selects the highest minor version supported by both peers and writes it into `ServerAccept` and all subsequent frames.
The accepted payload supplies both the generated session ID and the lobby-assigned player ID, allowing each side to construct the same immutable `ConnectionSession`.
Unknown message types or flags are protocol errors after acceptance.

### Client hello payload

| Field | Encoding |
|---|---|
| Minimum compatible protocol minor | 2-byte unsigned integer |
| Flags | 2-byte zero value |
| Participant name | 4-byte length and at most 128 UTF-8 bytes |
| Game build version | 4-byte length and at most 128 UTF-8 bytes |
| Gameplay asset manifest digest | 32-byte SHA-256 digest |

The build version is diagnostic information; protocol compatibility and the gameplay asset digest determine whether the host accepts the client.

### Server accept payload

| Field | Encoding |
|---|---|
| Session ID | 8-byte nonzero unsigned integer |
| Assigned player ID | 8-byte nonzero unsigned integer |

### Server reject payload

The rejection payload is a 4-byte length followed by at most 4,096 UTF-8 bytes suitable for display to the connecting player.

The asset digest covers the selected modpack manifests and gameplay data, not generated build paths or graphics that do not affect simulation.

## Commands and replication

Clients send `CommandIntent` messages with a client-generated command ID, command kind, referenced entity IDs and command arguments.
The first command kinds are Builder move/build and Hunter horse move/attack-move.
The command carries no player ID or client simulation timestamp.
The host uses the connection's assigned player, checks entity ownership and ability, and stamps accepted work with authoritative simulation time and a monotonic world revision.

The host returns `CommandResult` for every intent so rejected input remains visible to the client.
A simulation callback may complete well after the connection closed or a new match started. The host now binds each response to its source `SessionIdentity` (session ID and fixed player ID) and the local `RelayLobbyClient` connection generation. A callback for a departed Builder, a different player, or an earlier F10/rematch generation cannot enqueue a result into a new session.
The guest keeps a bounded set of outstanding command IDs and their kinds. An unrecognized or duplicate result is a protocol failure, not a silently accepted acknowledgement. Accepted/rejected special orders and the number still awaiting the host are displayed in the gameplay HUD. A rejected result clears that pending input but does not advance `ReplicationTracker` or mutate the replicated world.
Its world revision identifies the host state that made the decision; only an accepted snapshot or contiguous delta advances client state.
Accepted mutations appear in ordered `WorldDelta` messages.
`WorldSnapshot` contains the initial state and is also the recovery boundary if a client misses a revision.
Both replication messages carry the authoritative simulation time, world revision and the base revision needed to apply a delta.
Clients apply only contiguous revisions and request a snapshot after a gap.
`network::ReplicationTracker` owns this client-side revision state and prevents repeated recovery requests while a snapshot is in flight.

The initial implementation has no client prediction.
Clients render the reconstructed authoritative world and may interpolate presentation positions without changing simulation state.
The guest presenter stores received positions as timestamped continuous curves and advances a separate presentation clock between authoritative samples.
Playback initializes at the snapshot time and buffers half an observed update interval before consuming movement, leaving room for delivery jitter.
The reserve adapts to observed delivery jitter, capped at one update interval, so faster updates can tolerate a late packet without accumulating an unbounded delay.
It measures host clock drift over the stream rather than individual packet gaps, with two-percent rate corrections outside a buffer dead band, so clock drift does not repeatedly exhaust the available samples.
During steady delivery, visual delay is roughly one and a half update intervals (about 75 ms at the default 20 updates per second), in addition to transport and capture scheduling delay.
Playback stops at the newest received simulation time without extrapolating movement and rebuilds its buffer after starvation.
If presentation falls more than four observed update intervals behind, it resets to the current authoritative sample instead of retaining stale input response during a long catch-up; this can produce a position correction after a substantial stall.
New samples preserve the displayed position and any unplayed keyframes; unchanged positions provide stationary endpoints.
World sprites and their health bars use the same presentation time while the guest simulation remains paused.
Repeated position updates preserve the sprite animation start time.
Recovery snapshots reset presentation curves, including when a subsequent delta reaches the replication tracker before the next rendered frame.
Individual projectile launch records are sent when first observed by a
recipient, and retained for 3 seconds after impact so even short flights
between captures can be delivered; they expire without an arrow tombstone.
Guest renderers can play a late shot once from that event while never
reapplying hit damage. Recent launch history is capped at 2,048 events:
full no-loss handling at higher throughput remains an open OpenSpec task.

Entity snapshots also include up to 16 future keyframes from the simulation's
continuous position curve. The Hunter compares its next position sample with
the trajectory the recipient already holds, omitting an entity patch if
that plan still predicts the new position and all other entity properties
are unchanged. The guest advances its reconstructed position from that
same curve on each world revision, using updates as authoritative retasks
and corrections. No independent client pathfinding or damage simulation
is performed. Performance and animation-facing parity still need live tests.

## FVD authority and relay diagnostics

The relay and each game process use structured INFO messages with stable event
names so a role-based transfer can be followed without dumping frame payloads.
The **relay only knows rooms and forwarded channels**; it cannot decode the
authenticated Hunter/Builder lottery. The game clients log the actual authority
decision and the Hunter's new epoch.

Typical sequence when a Builder creates a three-player lobby:

```text
[FVD/authority] role_draw creator_player=2 hunter_player=1 participants=3 handoff=required
[FVD/authority] handoff_requested original_creator_player=2 hunter_player=1 participants=3
[FVD/authority] hunter_rehost_created epoch=2 room=4 expected_builders=2
[FVD/authority] handoff_invitation_forwarded epoch=2 room=4 builder_player=2
[FVD/authority] handoff_proof_accepted room=4 provisional_slot=2 original_player=2
[FVD/authority] handoff_committed epoch=2 hunter_player=1 room=4 participants=3
[FVD/authority] authority_active epoch=2 player=1 participants=3 room=4
[FVD/match] participant_disconnected player=2 role=Builder action=continue room=4
```

These are illustrative lines emitted by **different game processes**, not a
single global relay transcript. Room IDs, provisional slots, and assignment
order vary between matches. The standalone relay reports
`[FVD/relay] room_created`, `host_slot_reserved`, `pair_connected`,
`forwarding_started`, `forwarding_ended`, and `forwarding_error` with
room/slot identifiers. Its logs will show the original room's channels
closing while the replacement Hunter room's channels remain alive, but it
does **not** call this a host switch because it does not know the game roles.

Failed session startup emits `[FVD/match] session_error` with the original
reason. F10/disconnect cleanup emits `[FVD/match] local_session_closed` with
the previous phase and whether a replacement Hunter-owned session existed. The logs intentionally omit bearer tokens, one-time rejoin tickets,
raw session IDs, and complete protocol payloads. They log meaningful state
transitions, not every heartbeat or world delta, to avoid flooding an
eight-player match. Ensure the game and relay logging verbosity shows INFO
(and WARN for errors) when investigating a startup issue.

## Disconnect policy

Before match start, a disconnected participant is removed from the lobby and their player slot may be assigned again.
After match start, a Builder disconnect does **not** end the game. Their
units, towers, ownership and already accepted orders remain in authoritative
state. Their player ID is not reassigned, no further commands from their closed
session are accepted, and the remaining Builder(s) and Hunter continue.
The **Hunter (player 1) disconnecting ends the match for everyone**.
Every surviving remote is notified of a Builder departure, and their next
authoritative world revision records the player's disconnected status so
snapshot recovery cannot erase it. `network::MatchConnectionState` owns fixed
session bindings and rejects attempts to reassign disconnected participants.

**Hunter-owned authority from time zero (1.17c, awaiting Windows verification):**
The lobby creator is not necessarily the authoritative gameplay host.
If the original creator draws Hunter, the existing two-peer or multi-guest
host path continues unchanged. If the creator draws Builder, the remote
Hunter creates a new, independent `RelayMultiHostLobby`, and sends one
`AUTHORITY_REHOST_OFFER` for each original Builder through the original
authenticated session. The former lobby host forwards each offer to its
original recipient, including itself. All Builders authenticate a NEW relay
connection to the Hunter, send a one-time `AUTHORITY_REHOST_PROOF`, and
recover their original player IDs from the single validated StartMatch.
The old room does not bootstrap or advance a gameplay clock. After all
required guests have rejoined, the Hunter-owned room becomes the ONLY
authoritative simulation at time zero. The presenter switches its
host/guest role before requesting any world snapshot, and the new session
owns every subsequent command and delta. The former Builder lobby creator
can now disconnect without terminating surviving Builders or the Hunter.

These code paths are implemented but have not yet been compiled or verified
on Windows. The relay-client integration test was updated to exercise
Hunter-authoritative matches regardless of who created the initial lobby,
including a three-participant Builder departure and continued commands.
A handoff timeout ends an incomplete match rather than allowing two
authorities to simulate concurrently. The role draw occurs exactly once.
Protocol 7.4 uses single-use rejoin credentials bound to the original
participant slot, lobby revision, destination room, and authority epoch.
The original lobby host forwards these tickets and is trusted not to
impersonate other participants; this is not a Byzantine-secure or encrypted
Internet transport. Public room slots may also be exhausted by unauthenticated
join attempts before proof validation.

The multi-participant `MatchDisconnectCoordinator` keeps surviving
Builder sessions after another Builder disconnects, retaining original ownership and queued commands. The connection state now marks a match inactive when player 1 (Hunter) disconnects, refusing all further remote commands. The coordinator still broadcasts a final `ParticipantDisconnected` and world delta to surviving Builders so they can learn why the match ended. The live 3–8-player host worker sends those notices, drops only the
disconnected Builder's session and pending commands, and continues with the
remaining guests. Hunter loss ends the match. Matches now aim to
place authority on the Hunter before gameplay; unexpected failure of a
legacy Builder-owned authority still cannot be restored from `WorldState`,
and full mid-match restore remains a separate optional task.
During authenticated gameplay, the guest sends an empty, session-sequenced
PING once per second, and the host returns an empty, sequenced PONG. Both
accept ordinary gameplay messages as liveness evidence as well. Network
jitter and short periods without traffic are tolerated, but a peer that
sends no authenticated frames for 15 seconds is considered lost. Blocking
TCP frame sends have the same bounded 15-second deadline. Non-empty
heartbeats, invalid protocol sequence numbers and session mismatches
remain fatal rather than being treated as recoverable packet loss.
On TCP EOF or a fatal socket/protocol failure from the host, the client calls
`ReplicationTracker::host_disconnected()`, stops issuing gameplay commands,
and displays a match-ended/disconnected state. The tracker will then reject
snapshots, deltas and recovery requests; restarting a match requires a new
connection, fresh session and a new tracker. TCP EOF is not evidence of a
successful host migration.
Failure to enqueue a connection lifecycle event is fatal to the host session because continuing would conceal a disconnect from authoritative state.

Guest commands now include target-specific focus attacks and queued
construction; SnapshotRequest automatically recovers missing world revisions
without disconnecting. Host still validates ownership and target IDs.
Heartbeat exchanges are not a resume handshake: if TCP itself fails or the
15-second timeout expires, the session ends and all in-flight commands
are abandoned. Automatic reconnection would require a newly authenticated
session plus a fresh world snapshot and command-generation safeguards;
it is intentionally **not** attempted by this change.

### Rematching without restarting the client

After a live match, press **F10** to leave. The client closes its active relay
session, pauses and resets the shared game clock to time zero, clears old
world and terrain render objects, and asks the owning simulation thread to
rebuild the game, event loop, and entity factories. The graphics thread then
reattaches a fresh render factory. The lobby becomes available for Host/Join
only after this reset completes. A second room should start at revision 1
with fresh Builders and a full 20-second Hunter spawn countdown. Reconnects,
new rooms, and rematches should not inherit any previous entity IDs or orders.
The Windows two-client rematch still requires manual validation.

The host and solo construction HUDs request immutable presentation snapshots from the simulation thread.
Entity positions, construction orders and reserved tiles are captured together after event processing, so a frame cannot mix a removed foundation with its replacement tower's active order.
Placement previews, site markers, queue counts and construction progress use the same completed snapshot for each frame.
The next refresh is requested when the HUD reads a snapshot, bounding capture work to presentation demand.
Guests continue to derive these construction views from their accepted authoritative worlds.

The guest HUD reads its current Builder queue, construction phase, health,
score, and Hunter countdown from the latest recipient-authoritative state.
When precise construction progress cannot be reconstructed from the wire
snapshot, it displays the phase instead of an invented percentage.
