# Playable demo milestone

The current milestone is one understandable, reliable campaign loop:
start a demo, develop warp capability, build physical ships, reconnoitre a star,
complete a science survey, settle an eligible body, then save and resume.
The Windows download must run without a developer environment.

The current presentation target is **The First Light Expedition**, a guided opening
that reaches the same first-colony milestone in roughly 30 active minutes at the
player-selectable 3x speed. The guide exposes that pacing directly; pause remains
available for planning and Developer acceleration remains isolated from Player saves.

## Priorities

1. Stable startup and safe progress: complete resource imports, reject semantic
   runtime errors, confirm campaign replacement, and keep the game open if an
   exit save fails. Demo and normal campaigns use separate save slots.
2. A short, discoverable opening: explicit Play Demo / Continue Demo, a
   reproducible scenario and optional accelerated clock, visible next steps,
   ship building and selected-star commands. Normal campaign rules remain intact.
3. A complete loop: real research, construction, resources, passenger population,
   observer knowledge and valid settlement commands. Tests must not grant the
   completion state they are supposed to verify.
4. A usable Windows package: self-contained runtime and public data, exact
   source revision, checksums, an actual Windows startup check and rendered
   interface review.

## Evidence behind the focus

The next accepted product priority is graphical clarity. The default campaign
screen gives space to the star map, with a compact resource bar, an icon navigation
rail and a selected-target command dock. A single scrollable drawer holds research,
industry, ships and other detail views. Progress uses visible bars and demo steps;
orbital views show shaded, survey-safe planets. Validate actual pointer routing,
legibility and panel bounds in the rendered game before accepting the package.

The 100-system catalog now occupies one complete campaign-scale barred spiral rather
than a tiny sector inside a 32,000-unit backdrop. Galaxy artwork, procedural arm dust,
catalog stars and hit testing share the same 2,200 by 1,650 world frame. This scale is
specific to the current small campaign and must be reprofiled when larger galaxy-size
options become playable.

The maintained 100-system Player progression now uses the live Adaptive Research
authority and capability adapters from start to finish. Seed 20260908 constructs
the physical research, launch, shipyard and warp-test infrastructure, follows all
thirteen research projects, builds three ships, completes reconnaissance and four
detailed surveys, and settles with 250M conserved passengers. First settlement
currently takes about 12.1 active minutes at uninterrupted 8x; the same ordinary
rules complete in about 241 seconds with the explicit 24x Developer accelerator.
Research waiting dominated the opening. Adaptive laboratory throughput is now calibrated to
400 RP per Effective Research Lab per year, and a maintained calendar test holds the complete
thirteen-project Human warp path below 20 in-game years (currently about 15.7 years with the
Planetary Research Network). Idle Industry now has visible physical storage
capacity, so ships consume current reserves and ongoing production instead of an unlimited
stockpile. Further pacing work should use the maintained live-path measurement rather than the
retired six-project research model.

The first combined runtime failed because an editor scan was stopped before
SVG imports finished. Startup now waits for import completion and rejects
engine errors even when the process exits zero.

The recurring topology-checker CLR crash was repaired and validated separately
on the canonical research branch. That incident does not justify importing
unfinished research expansion into the demo.

## Deferred until the loop is playable

- Research content expansion beyond the integrated Adaptive Research campaign path.
- The 1,000-system astronomy catalog and large-galaxy performance milestone.
- New diplomacy, combat, AI or logistics features that do not unblock this loop.
- Release to main. Reviewed work continues through integration.

Existing work and branch histories stay preserved. The Core handoff records
the exact candidate, completed checks and remaining demo gates.
