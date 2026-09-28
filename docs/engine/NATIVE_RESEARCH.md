# Native research controller

`app/native_client/native_research_controller.hpp/.cpp` is a client-owned C++ projection and
command adapter over the current `CampaignFrame` research owner. It returns
owned strings and values, binds selection to the caller's campaign generation,
and rejects stale command revisions. It never retains simulation pointers.

The window contains only nodes at `Investigable` or later and edges between
them from Core's observer-safe `AdaptiveResearchView`. This matches the source
workspace's `IsDetailed` policy: rumored and hypothesized entries remain
unnamed until the player can investigate them. Domain tabs are derived from
those detailed visible nodes, and
search examines only their visible names, domains, solution families, and
known capability names. Hidden catalog entries are never projected or used as
search metadata.

Start, pause, and resume call `AdaptiveResearchCampaignCommands`. Quotes and
first-day commitments come from `AdaptiveResearchFundingPolicy` and the
campaign command helper. Readiness, program-capacity, minimum-lab, first-day
affordability, paused-reason, and resume-operating-cost gates project the same
authoritative values used by the source workspace; the canonical command still
makes the final admission decision. Current Core has no campaign-level cancellation API,
so active nodes expose cancellation as unavailable with an explicit reason.
The adapter does not synthesize cancellation or refund behavior.

Every command carries the window's campaign generation and research revision.
A generation change clears the controller's selected node, and commands from an
older window are rejected before the current campaign or treasury is queried or
mutated. A revision change likewise requires a refreshed window before action.

The focused harness covers an actual-source Player17 campaign and a canonical
500-system native fresh campaign. It checks hidden-knowledge search and command
rejection, visible-only tabs and edges, malformed UTF-8, generation selection
invalidation, owned snapshots, authoritative cost/precommit denial without
mutation, canonical start/pause/resume, retained paused progress, and the
explicit cancellation gap.

## Player workspace and persistence validation

### Inspector readability checkpoint (2026-09-15)

Cost, known capability, requirement, disabled-action reason and result-notice
sections use the native renderer's wrapped text measurements. The inspector
scrolls independently of the graph; its action and concise result remain
pinned. Long final lines are reachable. Routine progress, lab and funding
refreshes preserve the reading position, with clamping when content shrinks.
Selection, campaign, viewport and newly issued notices reset the position.
This repairs fixed-height clipping without changing research rules or adding
unrevealed capabilities or invented effect descriptions.

The workspace regression covers long wrapped sections, final-line reachability,
overscroll reversal, live refresh/shrinking content, selection/campaign/viewport
resets, graph isolation and the pinned action hit target. The final native build
and focused `native_research_workspace` CTest passed. The Python export tests
pass 62 checks, including malformed, missing, wrong-size and truncated capture
rejection and exact inspector diagnostic validation.

Two actual Vulkan runs use fresh 1280x720 and paused reload 1920x1080. Both keep
the base capture and an inspector-end sidecar after normal wheel routing;
actual font measurements check that the last displayed section reaches its
final line without covering the action. All four captures were visually
inspected. The fresh canonical program's content fits without overflow after
the spacing correction; the deliberately oversized text cases are covered by
the workspace regression, not claimed as a live campaign scenario. Funded
research advances and the entire paused Player17 recapture matches except
`SavedAtUtc`. Evidence: `work/native-research-inspector-runtime.json`,
`work/native-research-inspector-build.log`,
`work/native-research-inspector-tests.log`, and
`work/native-audio-validation/package-research-{started,loaded}*.bmp`.

These are local preview checks, not a sealed release or finished research art.
The remaining purpose/bonus prose gap requires authored catalog content; the
presentation must continue to display only the observer-approved data it has.

The native client workspace consumes this owned projection. It exposes known
research by domain, local text search, mouse selection and pan/scroll navigation.
The inspector displays the canonical funding quote and current action. The
catalog does not contain node-purpose or gameplay-effect prose, so the native
adapter does not invent benefits: it displays known capability names only when
the Core observer policy reveals them. Campaign cancellation remains unavailable.

The controller's query overload filters nodes while retaining detailed graph
metadata. Consumers must either use the complete owned window and filter locally
or guard edges and selection against their filtered node set. The client uses
the complete window, with generation/revision and economy/lab inputs invalidating
its cache. A changed campaign discards selection and notices; a stale command is
rejected before mutation. A stable frame does not rebuild the research catalog
view on every draw.

`--research-smoke <capture.bmp> --save-path <isolated-path>` is a development-only
player-input replay. It opens Research through the same mouse-event route, selects
an affordable program, invokes its action, starts time, and manually saves after
progress. With `--load`, it opens the saved campaign paused, uses the normal
campaign update, and performs another manual save. Both runs must report
`save=ok`; leaving the existing file untouched does not count as a round trip.

The export validator checks the player civilization's current research snapshot:
an active program must have positive research points, and the economy must record
positive research spending. It then compares the entire paused recapture, allowing
only `SavedAtUtc` to change. Runs use a separate working directory, restricted
Windows PATH and an isolated save; neither touches the default player slot.
Screenshots are retained beside the sealed export. Unit regressions reject
zero-progress/unfunded programs, changed treasury and a skipped loaded save.

## Source currency presentation

The native research controller continues to keep every funding quote in Core's
raw budget units for eligibility and commands. It now also owns the player's
`SovereignCurrencyDefinition` and strings formatted by that definition for the
treasury, authorization, milestone commitment, operating cost rate, estimated
total, and full reserve needed to start.

The workspace consumes those strings directly. Its local decimal `cr` and
round-up helpers were removed, so presentation no longer assigns a generic
currency to species-specific values or creates a second reserve rounding rule.
Normal daily costs use `SovereignCurrencyDefinition::format_rate` with a
negative value, matching the preserved source cost presentation.

Core's formatter displays two fractional local-currency digits. When a raw
amount is positive but below that visible quantum, the owned explanatory string
uses `Under ` followed by Core's formatted 0.01 local-currency amount. Daily
costs append `/day`. Zero remains Core's ordinary zero. Raw values remain exact
and the canonical funding comparisons are unchanged.

`NativeResearchWindow::funding_revision` changes only when the complete player
currency identity changes: species, currency name, code, symbol, or local-units
conversion. Treasury movement is deliberately excluded: the canonical command
revalidates current funds, so an ordinary economy tick cannot invalidate an
otherwise affordable click from a throttled window. Commands carry the currency
revision in addition to campaign generation and research revision. They reject
a stale currency view before mutation. Accepted commands advance the canonical
research revision; they do not churn the independent currency revision.

Focused tests cover human UED and pelagic Tide Mark ownership, every normal
canonical quote string, tiny-positive nonzero wording, treasury-driven funding
presentation without revision churn, an affordable balance change followed by
a canonical accepted Start, stale-currency rejection, and the existing 720p
clipping proof with the longer currency names and symbols. Strict Debug and
Release controller and workspace harnesses passed with MSVC C++ latest,
`/W4 /WX`, UTF-8, and precise floating point.
