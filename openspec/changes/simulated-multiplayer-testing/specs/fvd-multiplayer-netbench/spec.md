# Spec Delta

## ADDED Requirements

### Requirement: Automated five-client relay benchmark

A standalone opt-in benchmark SHALL run at least five authenticated FVD
network participants with one Hunter authority and four Builder recipients
through the actual public-room relay, without human input.

#### Scenario: Five participants issue orders
- **WHEN** four scripted Builders submit commands through their real sessions
- **THEN** the authority receives all commands with their correct immutable
  identities and all four receive matching authoritative responses

### Requirement: Separate simulated network conditions

The benchmark tooling SHALL allow distinct simulated latency, jitter,
bandwidth and temporary downstream stalls per remote player without modifying
normal production network behaviour.

#### Scenario: One slow Builder
- **WHEN** one of four Builder links stalls while the other links remain normal
- **THEN** each healthy Builder's observed command response and world-update
  delivery time is reported separately, and the stalled Builder's recovery is
  reported rather than inferred from aggregate latency

### Requirement: Honest performance metrics

The tooling SHALL distinguish transport turnaround and scripted world delivery
from actual gameplay simulation and visual responsiveness.

#### Scenario: High projectile load
- **WHEN** synthetic projectile records are supplied to network state updates
- **THEN** the report labels them as synthetic records and records wire-load
  delivery time without claiming actual Hunter combat or rendered FPS

### Requirement: Repeatable and safe teardown

The benchmark SHALL use bounded waits and shall close local listeners,
connections and proxy workers after success or failure.

#### Scenario: Multiple runs
- **WHEN** the same test is run repeatedly with the same profile and seed
- **THEN** each run starts with fresh network sessions and emits its own
  machine-readable report without leaking a running relay
