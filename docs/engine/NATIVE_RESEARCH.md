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
