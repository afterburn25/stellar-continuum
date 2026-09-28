# Galaxy presentation repair — 0.1.6 Alpha

Base: integration `7be839d3` (0.1.5 Alpha, PR #318).
Branch: `work/galaxy-view-repair`.

## Reported regressions

The 500-system profile made the overview artwork rectangle zero-sized and explicitly
excluded that profile from galaxy rendering. This removed the galaxy rather than
fitting the existing artwork to the new map. The regional zoom ceiling was 48, and
unidentified selections exposed synthetic labels such as `CATALOG 001` and
`ASTRONOMICAL TARGET` instead of player-facing unknown status.

## Repair scope

Restore the existing spiral galaxy artwork in the overview, using a shared fitted
frame for artwork and camera navigation. Preserve the measured 3D travel distances,
star identities, save data, survey rules, and established solar-system sun rendering.
The spiral is a strategic presentation of the playable map; the physical source
remains the 500 nearby-star catalogue documented in the 0.1.5 handoff.

Expand cursor-anchored regional zoom and retain selection, dragging, known-system
entry, and unknown-system privacy. Use `Unknown` consistently for unidentified
map selections and inspection output. Keep the cached star rendering introduced
by the preceding performance repair.

## Release verification

The game build is clean. Quality validation passed 20/20. Python Godot smoke and
Windows package checks passed 39 and 11 tests respectively. Native regional receipt
`work/galaxy-repair-regional-final` at source `4483e4e4` exited 0 with empty stderr
 at both 720p and 1080p; free zoom to the regional ceiling 192, known-system entry
 threshold 18, and unknown-system
privacy passed. Nearby receipt at source `36232526` passed 500-system fit, pan and
point-picking checks. These presentation changes add a spiral frame only: they do not
alter measured coordinates or distances. Discovered systems retain catalogue
identifiers; unexplored selections display `Unknown`, and no save restart is needed.
Final performance and hosted package provenance remain separate release evidence;
these receipts make no universal 60 FPS claim. Do not reuse the 0.1.5 ZIP.

Native performance receipt `work/galaxy-repair-performance/performance.json` at source
`4483e4e4` exited 0 with empty stderr. On an RTX 3080 Ti at 2560x1440, uncapped five-
second samples across 12 views measured 83.22–645.19 FPS. Galaxy overview measured
84.08 FPS with p95 12.80 ms and maximum frame time 15.66 ms; surface measured
303.49 FPS with p95 4.06 ms and maximum 22.12 ms. This does not establish universal
60 FPS. Final hosted gates remain pending.
