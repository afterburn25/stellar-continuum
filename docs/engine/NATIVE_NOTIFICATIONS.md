# Native recent events

The campaign's Events button opens a dated, newest-first history. The feed
keeps at most 32 reports; opening acknowledges the retained reports. Mouse
wheel scrolling reaches the complete retained history, with measured wrapped
text, category/date areas and a pinned close control. The panel is opaque so
underlying workspace labels cannot show through. Escape closes it first.

## Publication and ownership

`NativeNotificationFeed` and `NativeNotificationView` are presentation-only.
The campaign client owns them; Core, NativeCampaignSession and Player17 are
unchanged. `publish_campaign_notifications` retains the same audience-filtered
category/count summaries already used by transient feedback: research,
construction, ships, survey, contact, settlement and combat. Accepted research,
shipyard and construction commands also retain their existing player-visible
outcomes. This does not add simulation events or a second audio/voice path.

`NativeDiplomaticNotifications` consumes Core's observer view at bounded
one-second intervals and immediately following an accepted diplomatic command.
It defends against foreign-audience rows, requires identified participants
before adding a counterpart link, and never copies raw internal event text.
An unidentified participant yields an anonymous report without a link. The
supported report kinds are contact, communication, proposal sent/accepted/
rejected, agreement activated/terminated and war declaration. The event cursor
retains at most Core's 256 history identifiers and handles duplicate IDs.

Admission and load seed that cursor without replaying historical reports.
The 32-item feed is session-local and clears on campaign activation. It is not
a persistent journal; no new Player17 field is introduced. Economy funding
transition reports, general fleet-order history and detailed tactical reports
remain outside this slice.

## Input and navigation

The Events button shares the main HUD layout/hit rectangles and reserves space
from status text. It is unavailable under menus/settings and stronger
settlement, surface, diplomacy, production-cancellation or fleet-preview
confirmations. The panel handles its input before underlying workspaces.
Matching press and release are required for Close and OPEN RELATIONS. Dragging,
focus cancellation, scrolling after a press, stale contacts and layout changes
cancel link activation. An owned press remains consumed until release; orphan
releases and right clicks inside the panel cannot trigger the world behind it.

A valid link closes other workspaces and refreshes Relations before selecting
the identified counterpart. Missing contacts receive an explanatory notice;
the link does not invent knowledge, establish communication or issue a treaty.
All existing map navigation, keyboard/save behavior and simulation rules stay
in their existing paths.

## Integration review

Selectively adapted from Devin `8f6b720a` on `cpp/devin-swe2-native-conversion`.
The incoming fixed-card panel did not expose most of its retained history,
used release-only activation and treated an internal target ID as identified.
Raw event harvesting could disclose names; a separate audio loop would duplicate
existing coalesced feedback. Those paths were replaced by the bounded view and
safe publication adapters above. The existing diplomacy replay was extended
instead of importing a second fixture-incompatible notification smoke mode.

## Validation (2026-09-15)

- Final native MSVC build passes with /W4 /WX for the new logic targets.
- Five focused CTests pass: native_notifications, native_notification_events,
  native_ui_layout, native_client_input and native_diplomacy_workspace.
- Thirty-eight Python diplomacy/navigation tests pass, including malformed,
  duplicate, mistyped and false notification evidence, wrong target, replayed
  history, duplicate captures and canonical save mutations.
- Two relocated Vulkan launches use an isolated 20-system Player17 fixture.
  At 1280x720 the actual Accept click produces exactly two new diplomatic
  reports; Events changes unread 2 to 0 and OPEN RELATIONS changes selection
  from another empire to the correct counterpart. At 1920x1080 paused reload
  opens an empty history with no repeated alerts. Opening/reading/closing is
  checked against the full canonical payload and unchanged paused clock.
- The maintained diplomacy export validator requires four extra correctly
  sized, complete, nonuniform screenshots, keeps its original six captures,
  verifies only the expected agreement mutation and checks complete paused
  save equality excluding SavedAtUtc. Source fixture hashes stay unchanged.
- Two neighboring relocated 500-system navigation launches pass at 720p/1080p,
  covering existing galaxy/system shortcuts, blocked contexts, typing and
  F6-only save with unchanged canonical state. Both notification panel captures
  and the contact destination were visually inspected after final layout fixes.

Evidence: `work/native-notification-final-build.log`,
`work/native-notification-focused.log`, `work/native-notification-runtime.json`
and `work/native-notification-navigation-runtime.json`. Runtime captures are
under `work/native-audio-validation/package-diplomacy-diplomacy-*.bmp`.

Reproduce using `stellar.build_environment()` from tools/stellar-export for
MSVC INCLUDE/PATH, build stellar-continuum-native and the notification logic
targets, run the five named CTests, then run
`validate_native_diplomacy_export(package, env, player17_fixture)` and
`validate_native_navigation_export(package, env)`. These invoke real native
input routing and Vulkan rendering, not physical mouse automation. The
32-item scrolling and malformed-gesture cases use maintained logic tests.

This is an integration checkpoint, not a sealed download, full UI parity,
clean-machine certification or a sustained 60 FPS result. The separate
full-3D, tactical and orbital-placement contracts remain open.
