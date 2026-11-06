# Spec Delta

## ADDED Requirements

### Requirement: Engine API is a tracked FVD input

The source tree SHALL contain the complete nyan engine API required by FVD as versioned source data.
Native builds and packages SHALL consume that data without invoking an asset converter or API exporter.

#### Scenario: Clean checkout builds FVD
- **WHEN** a developer configures a clean checkout with the documented native build dependencies
- **THEN** the game and package use the tracked engine API
- **AND** no conversion command runs or writes generated API data into the source tree

#### Scenario: Tracked API is incompatible
- **WHEN** the tracked API version or manifest does not match the engine requirement
- **THEN** configuration or packaging fails with the expected and actual versions
- **AND** no tool attempts to repair or regenerate the data automatically

### Requirement: C++ tests run without Cython

The project SHALL provide a native executable for registered C++ tests.
The executable SHALL support listing tests, selecting a test by name and running all C++ tests without loading Python or a Cython extension.

#### Scenario: Named C++ test fails
- **WHEN** a developer runs a named C++ test that throws or triggers a test assertion
- **THEN** the runner exits nonzero
- **AND** the output identifies the test and preserves its C++ failure message and source location

#### Scenario: All C++ tests run
- **WHEN** a developer requests the complete native C++ suite
- **THEN** every registered retained C++ test runs exactly once
- **AND** the final process status reports whether any test failed

### Requirement: FVD has no Cython integration

The supported FVD source build SHALL contain no Cython extension modules, `.pxd` interface generation or Python adapter link path.
Native game, relay and C++ test targets SHALL NOT require Python headers, a Python library or Cython.

#### Scenario: Native development dependencies omit Cython
- **WHEN** a developer builds the native game, relay and C++ tests with a Python interpreter available only for code generation
- **THEN** configuration and compilation succeed without Cython or Python development libraries
- **AND** no compiled Python extension is produced

### Requirement: Native C++ sources do not require Python generation

The coordinate type families SHALL be checked-in ordinary C++ headers and source files.
C++ test registration SHALL be owned by native source rather than a Python-generated dispatch table.
Native targets SHALL NOT invoke Python or Mako to discover or produce C++ translation units.

#### Scenario: Native build starts from a clean checkout
- **WHEN** a developer configures and builds the native game, relay or C++ tests without Python installed
- **THEN** every required C++ source is already present in the checkout
- **AND** no source-generation command or generated-source cache is required

#### Scenario: C++ test is added
- **WHEN** a developer adds a registered C++ test
- **THEN** its native registration is reviewed in the same source change
- **AND** no Python test list or generated C++ file must be refreshed

### Requirement: Original-game asset conversion is unsupported

FVD SHALL treat its checked-in game data, graphics, audio and tracked engine API as authoritative source assets.
The project SHALL NOT provide a command for converting an original Age of Empires installation into FVD runtime data.

#### Scenario: Developer follows the asset workflow
- **WHEN** a developer adds or changes an FVD asset
- **THEN** the documented FVD-native asset tools and formats are used directly
- **AND** no upstream converter cache or converted modpack is required

### Requirement: Native executables are the only launchers

The supported game and relay launch paths SHALL be `openage-native` and `openage-relay`.
Build and editor launch commands SHALL build the corresponding native target before starting it.

#### Scenario: Source changed before launch
- **WHEN** a native source file is newer than its object file and a developer invokes a supported build-and-run command
- **THEN** the stale target is rebuilt before the executable starts

#### Scenario: Player starts FVD
- **WHEN** a player starts the installed game or relay
- **THEN** no Python command dispatcher, embedded interpreter or compatibility launcher is involved

### Requirement: Packaging is FVD-native only

The supported package workflow SHALL produce only the native FVD game and relay distribution.
It SHALL NOT create or install the generic openage Python package, embedded interpreter, legacy launchers or Cython extensions.

#### Scenario: Native package is inspected
- **WHEN** a generated FVD archive is audited
- **THEN** it contains the native game, relay, required libraries, configuration and validated FVD assets
- **AND** it contains no generic launcher, Python runtime, Python module or Cython extension

### Requirement: Retained Python tests have an explicit boundary

Relevant pure Python tests MAY remain as a developer-only workflow.
They SHALL NOT generate required C++ sources, import a compiled `openage` extension or become part of the game, relay or player package.

#### Scenario: Pure Python tests run
- **WHEN** a developer invokes the documented optional Python test command
- **THEN** the retained tests run independently of native target configuration
- **AND** the native source tree remains buildable without that Python environment
