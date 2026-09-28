# Native civilian mission recovery

The fleet panel exposes Hold/Resume and Return to Base for the selected owned
scout, science or colony ship. Logistics and military vessels retain their
existing command surfaces. This selectively adapts Devin `263b4396`; it does
not import that branch's host, audio, inspection or logistics implementations.

## Authority and confirmation

The controller dispatches the existing Core civilian recovery commands. A
command carries campaign generation, observer and fleet identity, role,
mission revision, hold/return state, destination system/body, settlement body
and elapsed establishment days. Any mismatch rejects the old command before
mutation. Core still decides eligibility, fuel/reach, nearest owned base,
lane completion and paid-colony abandonment. No movement, price or save rule
is replaced in the presentation layer.

Return on a paid colony mission first shows Core's full warning, including
no refund, lost establishment progress and retained colonists. Confirmation
moves to the other button, so a second click at the original Return position
cancels instead of silently accepting. Cancel does not mutate the campaign.
Changing selection, mission, progress, campaign, focus or opening the menu
invalidates pending confirmation. If establishment is actively advancing,
pause before reviewing and confirming the return.

Unselected rows carry no recovery quote. Frequent view refreshes do not run
route planning: reachability is assessed by Core on the explicit command.
Recovery messages pass through the existing observer-safe naming filter.
Travelling ships complete the current lane; recovery does not teleport them.

## Validation boundaries

Controller/workspace regressions cover hold/resume, stale commands, ownership,
selection, paid-work confirmation/cancellation, queued return in flight and
720p/1080p/4K action-row bounds. The Core civilian recovery parity test remains
unchanged. The packaged fleet smoke exercises real mouse hold/resume input on
a second civilian while preserving the routed ship, and requires complete
paused Player17 equality on reload. Paid abandonment is controller-test
coverage; this checkpoint does not claim a visual live paid-colony scenario.
