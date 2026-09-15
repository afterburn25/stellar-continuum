# Stellar Engine Editor 0.1.1

The user requested a launchable engine program, chose **Stellar Continuum first**,
and requested reusable Engine Assets. This adds an editor application to the
previous runtime-only migration scope. General-purpose editors were previously
deferred; the user's explicit request authorizes this focused first editor.

The isolated branch is `work/stellar-engine-editor`, based on native checkpoint
`e80e87f90563801166aeb68d8d8b8b9ec6ce79f3`. It does not alter migrated C++ rules.
The editor is a .NET 10 WPF Windows application, distributed self-contained.
Its Engine folder contains the validated C++ 0.1.9 executable and data. Generation
uses explicit arguments, a hidden owned process, bounded cancellation, a unique
temporary output, and the existing real `--seed-colonies` command. Projects cannot
choose a different executable. Runtime failures keep the last valid world.

## Working features

- Native galaxy/founding/colony generation, 250/500/1000/2500 systems and signed seeds.
- Detailed galaxy with a central bulge, spiral arms, dust lanes and unresolved stellar
  light; tilt, orbit, pan, zoom, selectable systems and an expanded workspace view.
- Map mode with class-based markers, focus and optional names.
- Searchable systems, bodies, civilizations and colonies with a property inspector.
- System display-name overrides, notes and bookmarks; bounded undo/redo.
- Atomic project saving with a previous-file backup, reopening and snapshot export.
- Persistent Engine Assets library, category/search filters, deduplicated imports,
  image/audio previews, portable embedded project assets and import on project open.
- Copied starter art/audio with original notices; optional tutorial and audition packs.

## Boundaries

This is an editor preview connected to the headless native runtime, not Vulkan
renderer parity or a complete campaign. Authoring annotations do not replace
authoritative generated properties. The existing migration status and blocked
graphical release presets stay unchanged. No campaign play, playable export,
scene/model editing, general scripting or full game-save adapter is claimed.
WindowsDesktop is an editor dependency, not a new native runtime dependency.

## Build and validation

Build with `dotnet build editor/Stellar.Engine.Editor/Stellar.Engine.Editor.csproj`.
Package using `editor/package-editor.ps1 -EnginePackage <validated-runtime>
-Destination <output>`, optionally adding `-TutorialAudio` and `-VoiceAuditions`.
The packager validates every native runtime manifest hash before copying it.
The Godot game excludes `editor/**/*.cs`, and editor/.gdignore prevents resource
imports. The baseline Game.csproj still builds independently.

Run the packaged application with `--verify <new-proof-directory>` to exercise
actual native generation, map hit selection, authoring, undo, filtering, native
provenance, asset preservation, project round-trips, atomic backup, malformed
project rejection, snapshot export and two rendered window sizes. Verification
uses its own asset library and projects. Screenshots render the actual WPF view.
A developer-machine relocated launch is not separate clean-machine certification.
The first packaged build passed 32 checks, including WAV decoding through the
Windows audio backend and restoration of portable project assets. The original
Godot project also built with zero warnings/errors after the editor exclusion.

Editor 0.1.1 passes 48 checks, including camera transforms, cursor-anchored zoom,
selection with depth, cached artwork across annotation edits, immutable world data,
view switching, panel expansion/restoration and generation at 2500 systems.
Rendered proofs cover the normal, compact, expanded and map views. See
[Galaxy view](GALAXY_VIEW.md) for rendering details and remaining limitations.

## Follow-up editor work

Integrate full campaign play only when the native campaign exists. Add editable
simulation properties through validated native commands, scene and model tools,
asset thumbnails/dependency-aware import and a Vulkan-backed game viewport.
Add playable build/export controls only after the native graphical release gates
pass. Keep authored asset identities and project migration/version handling stable.
