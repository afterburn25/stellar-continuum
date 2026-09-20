<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Consistent Solar System planet orientation

September 19, 2026.

Earth's saved axis node was being applied directly as camera-facing image roll.
In the affected campaign the node was 140.82 degrees, which inverted the globe.
Mars (196.37 degrees) and Neptune (141.12 degrees) were affected by the same rule.
The error was shared by the system view, planetary view and cached portraits.
The underlying maps and sphere's north-to-south UV convention were correct.

## ENGINE CAPABILITIES ADDED / EXTENDED

The canonical native planet `orientation` helper now routes `sol:` materials
through the existing `planet_presentation_pose` reference views. The modern
system globe, planetary globe and CPU portrait therefore agree with the legacy
Sol viewing convention. Earth, Mars, Neptune and the other reference globes stay
upright across campaign seeds and saved phases. Saturn retains its readable ring
tilt, and Uranus retains its deliberate sideways reference pose.

This uses the existing Engine quaternion, sphere and thumbnail interfaces. It
does not add a second simulation rule, change Core physical properties, edit
source pixels, migrate saves or enable automatic axial rotation. Manual globe
inspection still composes over the common reference pose. The imported-art path
continues to display its authored hemisphere in the original orientation.

There is no additional frame pass, texture load, retained resource or thread.
Existing immutable geometry/material and bounded portrait cache ownership remain.

Validation includes all ten Sol identities across saved roll, tilt and phase
variations (including the affected Earth/Mars values), north-versus-south portrait
colour markers, matching live material transforms, and imported orientations
through the existing 65-subclass material coverage. All four targeted suites
passed: canonical planet materials, planetary screen, system workspace and discs.
The packaged native replay passed for the nine Sol bodies with packaged surface
maps, both views, manual inspection, and eight imported classes. Visual review
confirmed upright Earth in both views, the other mapped Sol reference globes,
and unchanged Gaia/frozen artwork orientation. Package validation logs and
screenshots accompany this report.

## ENGINE LIMITATIONS REMAINING

These are stable reference viewing poses, not a reproduction of a particular
astronomical observer's camera or the physical pole direction in orbital space.
Uranus intentionally appears sideways. Manual inspection can still change the
view. Procedural non-Sol fallback worlds retain their seeded display pose, and
the existing reconstruction of unseen hemispheres in imported art is unchanged.

Future globe, thumbnail and map consumers should reuse the canonical orientation
boundary rather than applying saved physical axis metadata as screen roll.
