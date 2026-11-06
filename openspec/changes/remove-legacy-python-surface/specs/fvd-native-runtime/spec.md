# Spec Delta

## MODIFIED Requirements

### Requirement: Packaged assets are complete before release

The release pipeline SHALL package the tracked compatible nyan engine API and FVD modpack.
Package creation SHALL fail when required runtime data is missing, incomplete or incompatible.
The end-user game SHALL validate packaged data without running conversion or modifying packaged assets.

#### Scenario: Complete package starts
- **WHEN** the tracked engine API and FVD asset manifest match the engine compatibility version
- **THEN** native startup accepts the assets and enters the game

#### Scenario: Required data is missing
- **WHEN** the tracked engine API, FVD modpack or manifest is absent, incomplete or incompatible
- **THEN** configuration, packaging or startup exits nonzero with a message identifying the invalid data
- **AND** no command regenerates or modifies that data automatically

### Requirement: Developer Python tools remain available

Source builds MAY retain relevant pure Python tests as an optional developer workflow.
The supported native build workflow SHALL NOT require Python code generation, asset conversion, Cython, compiled Python extensions or the legacy Python command dispatcher.
Retained Python tools SHALL remain outside the end-user runtime component.

#### Scenario: Developer runs retained Python tests
- **WHEN** a developer runs the documented pure Python test command
- **THEN** the retained Python tests execute without loading `libopenage` through Cython
- **AND** no native target depends on that command having run
