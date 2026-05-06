# CLAUDE.md

Project instructions for Claude Code when working in this repository.

This repository is an Unreal Engine plugin for editor-side CAD robot assembly import and reconstruction. The plugin name is `CadRobotAssemblyImporter`, while the module names remain `CadImporter` and `CadImporterEditor` for compatibility.

## Core Working Style

These guidelines adapt the Karpathy-inspired agent rules from `forrestchang/andrej-karpathy-skills` for this project.

### Think Before Coding

- State assumptions when the request or code path is ambiguous.
- Ask for clarification when multiple interpretations would lead to different implementations.
- Point out a simpler or safer approach when it is materially better.
- Stop and name the uncertainty when the repository, Unreal API behavior, or workflow contract is unclear.

### Simplicity First

- Implement the smallest change that satisfies the requested behavior.
- Do not add speculative features, configuration, framework code, or abstraction layers.
- Prefer existing Unreal, module, and project patterns over new helper systems.
- If a solution grows larger than the problem deserves, simplify before presenting it.

### Surgical Changes

- Touch only files directly related to the task.
- Do not reformat, rename, or refactor adjacent code as a side effect.
- Preserve existing comments and compatibility decisions unless the task requires changing them.
- Remove only dead code introduced by your own change; mention unrelated dead code instead of deleting it.
- Every changed line should trace back to the user's request.

### Goal-Driven Execution

- Convert implementation requests into verifiable outcomes before editing.
- For bug fixes, prefer a reproducing test or a concrete manual verification path.
- For refactors, preserve behavior and verify before and after when practical.
- Keep looping until the stated success criteria are met or a real blocker is found.

## Project-Specific Notes

- Runtime code lives in `Source/CadImporter/`.
- Editor import and workflow code lives in `Source/CadImporterEditor/`.
- The plugin targets Unreal Engine 5.7, subject to the parent project's setup.
- Import/build workflows are JSON-driven and often involve master/child assembly descriptors.
- Preserve compatibility between the plugin descriptor, build files, module names, Blueprint-facing APIs, and existing assets.
- Treat generated Unreal directories such as `Binaries/` and `Intermediate/` as build artifacts unless the user explicitly asks to inspect or modify them.

## Verification

- Prefer focused checks that match the change: compile/build checks for C++ changes, editor workflow checks for import behavior, and targeted file inspection for documentation-only edits.
- If Unreal build tooling is unavailable in the current environment, say exactly what could not be run and what was checked instead.
