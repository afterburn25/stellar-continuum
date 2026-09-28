# Native planetary inspection

Selecting an observed planet or moon opens a grouped inspector in the native
orbital view. Physical facts, environment, and satellites/signals have separate
headings and aligned label/value columns. Names, survey status and actions stay
pinned while the measured body scrolls at smaller resolutions. Focus Planet
centers the selected body in the visible map field at the current zoom. Open
Colony remains a separate action subject to the existing ownership controller.

## Data and authority

`build_body_inspection` accepts only the detached NativeSystemSnapshot, never
raw campaign state. The existing controller admits only reconnaissance/full
survey of the current observer's selected system and restricts body membership.
Below reconnaissance or for a missing target, no inspector value is returned.
Full survey **and** a details record are required for exact radius, mass,
gravity, eccentricity, inclination, temperature, pressure and atmosphere.
Approximate geometry in a reconnaissance snapshot is not a confirmed reading.
Names, body kinds and observed positive signatures follow the existing catalog
policy. An absent signature is not a negative finding.

Radius is km, mass kg, gravity m/s², temperature K and pressure kPa; inclination
uses degrees and eccentricity is dimensionless. Nonfinite, invalid-range or
conversion-overflow values show Unconfirmed. Mass uses scientific notation;
large rounded distances avoid narrowing integer conversion. Known moon counts
include only non-self moon records attached to this body in the safe snapshot;
parent names resolve only within that snapshot. No foreign colony existence,
name or live economy value is exposed.

## Presentation lifetime and controls

The renderer supplies actual wrapped-text measurements. Detached text and
geometry cache until facts, viewport, footer or text measurement changes.
Only the visible portions of rows enter the draw list, with clipping before
the fixed action area. Long UTF-8 names have a bounded header allocation so
facts and actions remain reachable. A scrollbar shows remaining content.

Wheel input over the inspector scrolls its body; panel presses/drags cannot
move the camera or issue fleet orders. The focus button shares drawing and hit
geometry, works on uncolonized bodies, preserves zoom, and does not mutate the
simulation. Routine refresh replaces/redacts facts and invalidates removed
selection. Observer changes close the workspace; campaign discard clears the
inspector with its existing orbital state. Approved Sol artwork is unchanged.

## Maintained validation

- `native_body_inspection`: full/recon/detected gates, disclosure mutation,
  metric formatting, parent/moon membership and invalid numeric values.
- `native_body_inspection_panel`: measurement caching/invalidation, UTF-8
  wrapping, clipping, 720p/1080p layout, bounded scroll, target reset and clear.
- `native_system_workspace`: real focus input, uncolonized selection, camera
  isolation, downgrade redaction and observer invalidation alongside existing
  travel/selection/colony tests.
- `native_system_runtime.validate_native_system_export`: relocated Vulkan
  720p fresh and 1080p reload, actual application input routing, Earth focus,
  clipped physical/environment text, bottom-of-details capture, camera-stable
  scrolling and reset, and complete paused Player17 equality except SavedAtUtc.
  Missing/false proof, nonfinite scroll or missing captures fail export validation.

Local evidence: `work/native-body-build.log`, `native-body-ctest.log`,
`native-body-python.log`, `native-body-runtime.json`, plus system BMPs under
`work/native-audio-validation`. This is a native presentation integration,
not a Core rule/save-format change or a completed visual-parity claim.
