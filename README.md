# CadRobotAssemblyImporter

CadRobotAssemblyImporter is an Unreal Engine plugin for editor-side import and reconstruction of CAD robot assemblies using JSON-driven workflows.

## Scope

- Runtime module: `CadImporter`
- Editor module: `CadImporterEditor`
- Plugin display name: `CadRobotAssemblyImporter`

Note: module names remain `CadImporter` / `CadImporterEditor` for compatibility with existing code and build setup.

## What It Does

- Imports CAD-linked FBX assets with assembly metadata.
- Supports master/child JSON workflow for hierarchical robot assembly definitions.
- Builds Blueprint actors and can replace level hierarchy from workflow output.
- Provides runtime actor types for command/status integration (`ACadRobotActor`, `ACadMasterActor`).

## Typical Workflow

1. Prepare or select a master actor hierarchy.
2. Generate or load master JSON and child JSON files.
3. Run import/build from editor tooling.
4. Generate assembly Blueprints and apply level replacement if needed.

## Requirements

- Unreal Engine 5.7 (project-target dependent).
- Editor environment for import/build steps.
- CAD export assets (commonly FBX) and matching JSON descriptors.

## Repository Layout

- `Source/CadImporter/`: runtime actor/types module
- `Source/CadImporterEditor/`: editor import and workflow module
- `Resources/`: plugin resources
- `CadRobotAssemblyImporter.uplugin`: plugin descriptor

## License

This project is licensed under the Apache License 2.0.
See `LICENSE` for full text.
