# Fun visual vertical slice

Status: **production candidate pending exact-head rendered review**
Branch: `work/fun-visual-vertical-slice`

## Player experience

The opening is presented as **The First Light Expedition**. A new Human Sandbox
campaign begins on Earth in Sol. The map, department pages and surface remain fully
interactive under ordinary Player rules. The guide leads through three readable
milestones: develop warp flight, build the expedition fleet, then scout, survey and
settle a suitable world.

The maintained progression measurement is about 32 active minutes at 3x speed. The
guide therefore offers a visible **Begin at 3x** action and keeps pause available for
decisions. It does not grant resources, skip prerequisites or use Developer commands.

## Galaxy presentation and scale

The prior overview placed a roughly 1,800-unit 100-system coordinate field inside a
32,000 by 18,000 artwork rectangle. At overview scale the playable stars collapsed
into a small cluster that did not visually belong to the galaxy.

The campaign galaxy now uses a 2,200 by 1,650 world frame centered on the actual
barred-spiral generator offset. The rendered artwork, arm dust, star positions, camera
fit and mouse hit geometry use that same transform. The complete 100-system campaign
fills the galaxy, while the existing smooth wheel transition moves continuously from
the whole galaxy through the local stellar region and into the selected system.

This frame is deliberately sized for the current 100-system Sandbox. Future galaxy
sizes and shapes need their own generation and presentation profiles rather than
scaling empty space around the same catalog.

## New visual source

`assets/visual/space/campaign-galaxy-four-arm-v1.png` was generated specifically for
Stellar Continuum with OpenAI's built-in image generation tool on 2026-09-10. It was
not copied from another game and contains no third-party logo or interface.

Final generation prompt:

> Create an original cinematic strategy-game background: a complete face-on Milky-Way-like barred spiral galaxy with four readable sweeping arms, a warm gold-white core, cool blue and cyan outer arms, restrained magenta and violet nebula color, fine dust lanes, thousands of sharp stars, and sparse distant galaxies in deep black space. Compose it as a coherent navigable 100-star campaign map, with no labels, text, icons, borders, interface, watermark, or copied game imagery. High contrast and photorealistic astronomical detail, cinematic but clean enough for bright interactive star markers.

The runtime keeps this raster asset at its native aspect and overlays vector/catalog
stars at gameplay resolution, so zooming into the regional map replaces the overview
art with live sharp geometry instead of enlarging a low-resolution star layer.

## Original audio

The two music beds and six feedback cues under `assets/audio/` were created for this
branch using deterministic additive synthesis. They contain no sampled music, sound
library recordings or third-party material. The presentation layer provides separate
Master, Music and Sound Effects controls and stores those preferences outside campaign
saves.

Music contexts:

- `menu-continuum.wav`: restrained menu atmosphere.
- `deep-space-operations.wav`: low-intensity strategic campaign bed.

Feedback cues cover hover, confirmation, discovery/research, construction, ship launch
and strategic alert events.

## Validation boundary

Release and Debug builds, the maintained simulation suites, import/runtime smoke,
exact-head Godot screenshots and exported Windows startup remain release gates. The
screenshot journey includes the main menu, audio settings, galaxy overview, zoomed
region, system, Earth focus and surface views. Final production-ready status requires
reviewing those rendered frames on the exact published commit.

## Player-facing mechanic review

The slice judges existing mechanics by the decision they present, the result they
communicate and the next action they reveal. Simulation services remain authoritative.

| Mechanic | Player-facing treatment in this slice | Recovery / next decision |
|---|---|---|
| Map navigation | One aligned galaxy coordinate frame, labelled scale breadcrumbs, smooth wheel transitions, pointer-centered zoom and drag panning | Back/breadcrumb controls restore the prior spatial scale; the guide points to the relevant department |
| Research | Visible lab assignment, authorization, milestone reserve, operating cost, stage progress and available/mature state cards | Active work can pause and resume; blockers and the next investigable program remain visible |
| Economy | Sovereign balance, gross income, operating costs, net flow, finite materials and funding state reconcile on one page | Deficit and arrears states name practical recovery choices instead of silently producing resources |
| Construction | Selectable cards show material and currency cost before authorization, live progress, prerequisites and affordability | Unavailable work remains visible with its blocker; the expedition guide returns to the next required build |
| Shipbuilding | Art-led designs show role, travel performance, material cost and currency authorization before queueing | Locked capability/facility reasons stay visible; completed fleets appear in the owned-fleet view |
| Exploration | Mission phase, destination and ETA come from observer-safe read models; scout and science orders use the selected star | Invalid orders explain their current blocker; mission and colony-site tabs expose the next valid action |
| Colonization | Fully surveyed exact-body candidates show the chosen ship, suitability, reach and settlement commitment before funding | Core revalidates on commit and returns a useful rejection; surveying another candidate remains available |
| Colony surface | Graphic build catalog previews structures and exposes currency, materials, capacity, workers and power before placement | Placement can cancel; sites can pause, demolish, repair, prioritize, shut down and upgrade where the rules allow |
| Logistics | Supply, demand, delivery and shortfall are visual metrics backed by represented network nodes | A plain-language next-decision callout identifies staffing, power, funding or freight capacity as the recovery path |
| Diplomacy and combat | Observer-visible contacts, standings, proposals and conflict commands expose state and consequences through their cards and tooltips | Unavailable commands remain disabled; proposal response/withdrawal, ceasefire, hold and retreat provide rule-backed recovery paths |
| Save and recovery | Player and Developer campaigns use isolated save slots; loading presents a paused ready state and startup recovery uses the maintained backup path | Save failure keeps the campaign open and support export remains available |

Shared buttons now provide hover and commit audio, and major research, construction,
mission, colony and ship events add category-specific audio to the existing visual
notification feedback. Exact-head screenshot review remains the check for layout,
legibility and visual hierarchy at the supported minimum window size.
