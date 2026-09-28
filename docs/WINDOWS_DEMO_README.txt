STELLAR CONTINUUM - WINDOWS DEVELOPMENT BUILD

Extract the entire ZIP into a folder, then open StellarContinuum.exe.
Keep the executable, StellarContinuum.pck, data_Game_windows_x86_64, and
data folders together. The required .NET runtime is included; Godot and
the .NET SDK do not need to be installed.

This is a development build for Windows PCs with a 64-bit Intel/AMD processor.
It is unsigned. BUILD.json identifies its exact version, source commit,
official Godot tools, file checksums, and CI run.

START PLAYING

Fresh human campaigns begin on Earth in Sol. The other founding factions have
their own homeworlds. Sol contains Mercury, Venus, Earth, Mars, Jupiter, Saturn,
Uranus, Neptune and Earth's Moon. Open System, select Earth and zoom in to focus on the planet.
Planet imagery credits and source limitations are in PLANET_IMAGE_CREDITS.md.
Existing saved campaigns retain their original worlds; start a new campaign
to use the Sol start.

Choose Player for ordinary rules, or Developer for a separate testing campaign.
Developer accepts a reproducible world seed and offers a faster clock and guide.
Follow research and construction prompts, then open Ships and choose a named
design to build a scout, science vessel or colony ship.
Select a ship icon to inspect the vessel, then right-click a destination star.
Travel consumes game time and fuel. Scouts reconnoitre and science ships survey
after arriving; completed surveys do not prevent returning to that system.
Use the icon navigation rail for Research, Industry, Ships and Explore.
One detail drawer opens at a time; Map or its close icon clears the view.
Move a colony ship to the destination system, then right-click a surveyed world
to begin settlement. Point at the world first to review the cost and requirements.
Travelling alone never orders that ship to settle.
Open System shows known orbits; click a planet for its known classification.
Zoom out to see the complete Milky Way artwork, then zoom into the local
stellar region, a surveyed system and a selected planet. The breadcrumbs
and Back button move between views and restore the previous camera.
Inspect opens selected-star details.

Both modes use the same resource, research and ship rules. Developer adds
explicit testing tools and a 24x clock. Player offers 1x, 2x, 3x and 8x speeds.
Space pauses/resumes; 1-4 choose ordinary speed, and the speed selector offers
24x acceleration in Developer mode. Esc opens campaign options; Menu opens save and support.
Use the mouse wheel to zoom and left-drag to pan galaxy and system maps.
The compact left icon rail replaces the bottom command buttons. Planet and
ship details are grouped in a contextual panel on the right.
1920x1080 is the reference resolution. At 1280x720 pages reflow and scroll;
1440p and 4K retain the reference composition with native scene rendering.
F6 saves the campaign. F8 exports a support bundle.

BUILD ON A PLANET

Focus Earth and choose Surface. Left-drag moves the camera, middle-drag
looks around, and the wheel moves between street and bird's-eye views.
WASD movement is also supported.
Choose a generator, science lab or fabricator from the graphical build menu,
move its preview onto clear ground, rotate with R and click to place.
There are no fixed slots. Sites must clear other buildings, steep slopes,
the colony hub and the boundary. Construction spends available industry
over time; completed powered labs and fabricators increase production.
The right panel shows colony operations, building choices and selected-site
actions. Module and hub upgrades take time before their new capacity activates.
The surface header has Save, Pause and Return to orbit buttons. Escape
cancels placement, then returns to orbit. Buildings retain their exact
positions and construction progress when the campaign is saved.
See MAP_AND_SURFACE_GUIDE.md for controls, building costs and art credits.

The main menu and on-screen control hints describe the current candidate's
actions. The full game is in development; economy expenses, content and
long-term systems remain incomplete. There is no timed demo ending.

ORBITAL CONSTRUCTION

Open your home system and select a shipyard, launch complex or mining-network
site. Its right panel shows a 3D preview, requirements, cost, upkeep and build
time. Begin construction when the prerequisites and funding are available.
The structure gains visible sections as work advances. Completed stations use
the established infrastructure production and shipbuilding rules.

SAVES AND SUPPORT

Campaign saves and logs are stored in the Godot app_userdata/Game folder
under your Windows application-data directory. They are separate from the
download folder. Player and Developer campaigns use separate save slots
and backups. Open Player or Open Developer checkpoints the current campaign
before switching. Resume returns to the active campaign. Developer tools
require explicit actions and permanently mark their campaign Tools used.
See GAME_MODES.md for the controls, save isolation and development priorities.
Starting a replacement campaign requires confirmation.
If something fails, include BUILD.json and an F8 support bundle in the report.

VALIDATION

The final Windows artifact is published after the actual exported executable
starts successfully in a hosted Windows CI job. The package also passes file
and runtime completeness checks and a Linux exported-game startup check.
BUILD.json records which checks have completed. Automated headless startup
does not certify interactive input or graphics on every Windows PC.
