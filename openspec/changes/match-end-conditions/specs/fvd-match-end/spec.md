# Spec Delta

## ADDED Requirements

### Requirement: Hunter wins when all Builders are dead

The match SHALL end with "Hunter wins" as soon as no Builder player is still
in the game: every Builder unit is dead or its player has disconnected without
reconnecting.

#### Scenario: Last Builder dies
- **WHEN** the last living Builder is killed
- **THEN** every player sees the Hunter wins screen and no further orders are accepted

#### Scenario: Last Builder disconnects
- **WHEN** the only remaining Builder player disconnects and does not reconnect
- **THEN** the Hunter wins, and that player's towers are still standing

### Requirement: Builders win on time

The match SHALL end with "Builders win" when 45 minutes of match time have
passed and at least one Builder is alive. The timer SHALL pause while the match
moves to a new host and resume when it is running again.

#### Scenario: Timer expires
- **WHEN** match time reaches 45:00 with Builders alive
- **THEN** every player sees the Builders win screen

#### Scenario: Host changes
- **WHEN** the match moves to a new host at 30:00 and resumes 20 seconds later
- **THEN** the timer still shows 15:00 remaining when play resumes

### Requirement: A dead Builder's towers collapse

When a Builder is killed, all towers, towers under construction and
foundations owned by that player SHALL be removed, with finished towers playing
the collapse animation. A Builder who disconnects keeps their towers.

#### Scenario: Builder killed
- **WHEN** a Builder owning 12 towers is killed
- **THEN** all 12 collapse and their tiles become free

#### Scenario: Builder disconnects
- **WHEN** a Builder owning 12 towers disconnects
- **THEN** all 12 remain and keep firing
