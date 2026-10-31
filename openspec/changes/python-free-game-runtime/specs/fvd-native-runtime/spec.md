# Spec Delta

## ADDED Requirements

### Requirement: Packaged gameplay is independent of Python

The FVD end-user runtime SHALL start and run the game without an installed or
bundled Python interpreter.
No native binary loaded by gameplay SHALL import the Python ABI.

#### Scenario: Player starts the packaged game
- **WHEN** a player starts `openage` from an extracted or installed end-user package on a machine without Python
- **THEN** the lobby or requested solo game opens without starting or loading Python

#### Scenario: Runtime package is inspected
- **WHEN** the end-user package contents and native imports are validated
- **THEN** it contains no Python interpreter, Python shared library, standard library, Python module, bytecode file or Cython extension
- **AND** no runtime binary imports the Python ABI

### Requirement: Native startup preserves packaged game options

The native game executable SHALL parse and validate the command-line options
required to run packaged FVD gameplay and SHALL pass validated values to the
C++ engine.

#### Scenario: Default launch
- **WHEN** a player starts the game without command-line arguments
- **THEN** it loads the packaged FVD modpack with the documented window and networking defaults

#### Scenario: Solo override
- **WHEN** a player starts the game with `--solo`
- **THEN** it skips the lobby and starts local FVD gameplay

#### Scenario: Invalid option value
- **WHEN** a player supplies a nonpositive network update interval or invalid window setting
- **THEN** startup exits nonzero and reports the invalid option before initializing the engine

### Requirement: Runtime paths are resolved natively

The game SHALL discover packaged immutable data and writable user configuration
through native platform paths without Python filesystem objects.
Explicit asset and configuration path overrides SHALL remain available.

#### Scenario: Package is moved after extraction
- **WHEN** a portable package is extracted to a different directory and its executable is started there
- **THEN** the game finds its packaged libraries, QML and assets relative to that installation

#### Scenario: Explicit development asset path
- **WHEN** a developer supplies a valid asset-directory override
- **THEN** native startup uses that directory and leaves packaged assets unchanged

### Requirement: Packaged assets are complete before release

The release pipeline SHALL package a compatible exported nyan API and FVD
modpack and SHALL fail package creation when required runtime data is missing or
stale.
The end-user game SHALL validate packaged data without running conversion.

#### Scenario: Complete package starts
- **WHEN** the packaged asset manifest matches the engine compatibility version
- **THEN** native startup accepts the assets and enters the game

#### Scenario: Required data is missing
- **WHEN** the exported API, FVD modpack or manifest is absent or incompatible
- **THEN** startup exits nonzero with a message identifying the missing or incompatible data
- **AND** it does not run the converter or modify packaged assets

### Requirement: Packaged relay is independent of Python

The FVD relay distribution SHALL provide a native console executable that
validates its options, runs the existing C++ relay service and handles a normal
stop request without Python.

#### Scenario: Relay starts without Python
- **WHEN** an operator starts the packaged relay on a machine without Python
- **THEN** it listens with the requested address, port, capacity and access-token settings

#### Scenario: Relay receives a stop request
- **WHEN** the operating system sends a normal console or process stop signal
- **THEN** the native relay stops accepting connections, releases its listener and exits cleanly

### Requirement: Developer Python tools remain available

Source builds SHALL retain the Python-powered conversion, code generation and
test workflows needed to build and maintain the project.
These tools SHALL be outside the end-user runtime component.

#### Scenario: Developer converts assets
- **WHEN** a developer uses the documented source-build conversion command with its Python dependencies installed
- **THEN** conversion remains available and can produce the inputs required by release packaging
