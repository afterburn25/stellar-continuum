# Development, GitHub and evidence policy

The active native baseline is `cpp/codex-native-architecture-integration`.
Audit date: 2026-09-20. `main` and other research/editor workstreams have distinct
histories; their features are not automatically part of this branch.

1. Read [AGENT_HANDOFF.md](AGENT_HANDOFF.md), root `AGENTS.md` and the relevant
   source/tests before changing a subsystem.
2. Inspect status/upstream/worktrees. Preserve unknown work. Use a focused
   branch or isolated checkout when work overlaps another owner.
3. Keep reusable engine mechanics in Engine, game rules in Core and presentation
   in App. Record material decisions under `docs/decisions/`.
4. Build affected targets, run meaningful regressions and visually inspect
   rendering changes. A visibility test with uniform colors does not prove
   authored detail remains sharp; the flare regression demonstrated this.
5. Commit source, tests and required reviewed assets with the supporting docs.
   Update the project state, capability entry, known issues and handoff.
6. Push normally to the correct branch. Verify the remote commit. Do not silently
   rewrite shared history, change the default branch or manufacture release tags.
7. State tested revision/configuration, exact failures, skipped prerequisites and
   unverified performance. Preserve logs locally; publish compact sanitized
   receipts, not user saves, minidumps or enormous screenshots.

## Status vocabulary

| Status | Meaning |
| --- | --- |
| IMPLEMENTED | Concrete scoped capability, integrated owner/consumer and regression evidence. Does not imply every future extension is complete. |
| IMPLEMENTED BUT NEEDS POLISH | Working path with documented UX/quality/coverage limitations. |
| PARTIALLY IMPLEMENTED | Some real foundations or consumers exist; required work remains. |
| EXPERIMENTAL | Real exploratory code with acceptance/production suitability unresolved. |
| PLANNED | Agreed next work, no completed implementation claimed. |
| NOT STARTED | No implementation located in this branch. |
| BLOCKED | A specific unresolved prerequisite prevents the stated deliverable. |
| DEPRECATED | Superseded; preserved only for migration/evidence where needed. |

## Source control and large assets

Git LFS stores reviewed native PNGs. Run `git lfs pull` after checkout; pointer
files are not image data. Do not migrate existing shared history to LFS. Import
masters and review-only renders stay local; accepted prepared maps and the
manifests needed to build/export are versioned. Some cloud/thumbnail files remain
required by the loose exporter contract even though the shipping cooker excludes
them. That distinction is documented, not hidden by deleting files.

Do not commit `build-native/`, `work/`, `Builds/`, user data, installed payloads,
ZIPs, PDBs, dumps or signing keys. Do not delete untracked work merely to produce
a clean status. Document intentional exclusions and validate clone completeness.

## CI caveats

`stellar-engine.yml` is the native workflow, but its existing push filter covers
`engine/**`, not this `cpp/**` branch. It also still names removed surface-view
targets. `native-cooked-release.yml` is manual and needs a self-hosted Windows
GPU runner. Historical Godot/research workflows remain for their legacy branch
contracts; they are not native validation evidence. See issue SYNC-007.
Do not equate successful Git push with passing CI or a published GitHub Release.
