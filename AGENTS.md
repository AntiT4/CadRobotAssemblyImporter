# AGENTS.md

Instructions for Codex and other coding agents working in this repository.

This repository contains the `CadRobotAssemblyImporter` Unreal Engine plugin. It provides editor-side import and reconstruction of CAD robot assemblies through JSON-driven workflows. The plugin descriptor keeps the historical module names `CadImporter` and `CadImporterEditor`; do not rename them without an explicit compatibility plan.

## Agent Conduct

These rules are adapted from the Karpathy-inspired agent guidelines in `forrestchang/andrej-karpathy-skills`.

### Think Before Coding

- Surface assumptions when the task or codebase context is ambiguous.
- Ask when two reasonable interpretations would produce meaningfully different code.
- Push back when a simpler, safer, or more maintainable approach clearly fits the request better.
- Name confusion early instead of silently guessing about Unreal behavior, JSON schema shape, or editor workflow expectations.

### Simplicity First

- Use the minimum code needed to solve the requested problem.
- Avoid speculative features, broad configurability, one-off abstractions, and unrelated cleanup.
- Match the existing C++/Unreal style and local helper patterns.
- Rework the approach if the implementation becomes noticeably more complex than the requested behavior.

### Surgical Changes

- Keep edits scoped to the requested behavior and the files that directly support it.
- Do not reformat, modernize, rename, or refactor neighboring code opportunistically.
- Preserve comments, asset-facing names, module names, and Blueprint/API compatibility unless the task explicitly requires a change.
- Clean up unused imports, variables, or functions only when your own changes made them unused.
- Mention unrelated issues separately instead of folding them into the patch.

### Goal-Driven Execution

- Define the success criteria before making non-trivial edits.
- For bug fixes, prefer a reproducer or a clear manual verification path.
- For refactors, keep behavior equivalent and verify with the narrowest reliable check.
- Continue until the criteria are met or a concrete blocker is identified.

## Repository Map

- `Source/CadImporter/`: runtime actors, data types, and command/status integration.
- `Source/CadImporterEditor/`: editor import tools, workflow orchestration, JSON-driven assembly build logic.
- `Resources/`: plugin resources.
- `Content/`: plugin content.
- `ThirdParty/` and `ThirdParty.json`: dependency payloads and license metadata.
- `CadRobotAssemblyImporter.uplugin`: plugin descriptor.

## Verification Guidance

- Documentation-only changes can usually be verified with `git diff` and file inspection.
- C++ or Build.cs changes should be verified with the relevant Unreal build command when available.
- Editor import workflow changes should include a manual verification note when automated tests are not practical.
- If required Unreal tooling is unavailable, report the limitation clearly and describe the checks that were completed.
