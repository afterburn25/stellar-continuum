# Visual, voice and music polish handoff

Source milestone: Core branch `work/cohesive-visual-polish`, based on `10bedd6`.

This handoff records the combined presentation milestone for integration review.
It describes validated behavior and known limits; it does not promote the
procedural presentation to finished production art.

## Validated presentation work

- Core's full native capture validated exactly 33 expected-head images and 120
  real input checks. The immersive camera review passed all nine captures.
- Surface presentation now includes more detailed procedural canopies, entrances,
  podiums and roofs. Completed modules receive automatically generated access-road
  presentation geometry. Roads are presentation-only: there is no manual road
  drawing and no production or transport effect. The route solver has a 32-route
  cap, a bounded 18,000-node search, and preserves exact route endpoints; its
  obstacle regressions pass.
- UI work refined the palette, cards and compact header. Galaxy presentation uses
  cached smooth colored stars, safer 2D orbit fitting and a continuous galaxy
  shader with a compact 100-system shape matching the catalog. Ship trails follow
  authoritative active route positions and naturally remain stationary while paused.
  Existing 2D/3D navigation and saves remain intact.
- The existing private aged-save fix remains required and must be preserved.

The surface remains procedural and the galaxy still needs future custom
production artwork. These changes do not claim photorealism or finished AAA
presentation quality.

## Voice validation

Core's processed voice capture (from `aa53fac`) reported narrator peak `0.6251`,
grey `0.5363`, and alien `0.8913`. Runtime validation confirms one non-spatial dry
voice player, with no Chorus, Reverb or Delay effects. The Voice bus uses a
`-1 dB` post-DSP hard limiter. The prior echo was traced to an added 18 ms /
27 ms chorus plus reverb; the source path is dry.

## Music integration

The main score is the user-supplied `claimed_by_the_void_loop.mp3`, copied
without transcoding to `assets/audio/music/claimed-by-the-void-loop.mp3`.
SHA-256:

`25C81BEE74C37DC91F0895FA68DB72B026C028C65D951634D07CD4AE0B325FA2`

The runtime uses one non-spatial primary music player and Godot's MP3 loop flag.
Menu/game context changes retain the same player and playback position. Existing
music volume, voice ducking, SFX and voice-bus behavior remain connected. The
older WAV score files are retained as superseded historical assets. Provenance
records the user-supplied filename and hash only; no creator, license, CC0 or
original-synthesis claim is made.

## Automated validation

The following checks are attributed to the corresponding validation runs:

- Graphics full native capture on `10bedd6`: 33/33 expected-head images and
  120/120 real-input checks; immersive camera: 9/9 captures.
- Voice runtime on `aa53fac`: processed capture and single-dry-player checks.
- Plain-suite validation on this branch before presentation-only follow-ups:
  Quality 19/19, Simulation 70/70, Core 72/72, VoiceCore 12/12, and Godot
  Smoke 22/22.

Automated checks use isolated user profiles and Dummy audio output to avoid
unsolicited clicks. They do not change the user's audio settings.

## Native capture and performance status

The earlier native full capture run
`82898676b2adea84ada503ab56f1d20d9e1d5d7` produced all 33 expected images,
passed all 120 real-input checks, and passed the music loop, context continuity
and ducking assertions. Its shutdown leak was subsequently traced to Godot's
queued audio stop: stopping a player schedules fade-out deletion and an
immediate SceneTree quit can happen before the mixer retires the reference.
This is engine behavior documented in the pinned
[Godot audio server source](https://raw.githubusercontent.com/godotengine/godot/ed1daf0bf/servers/audio/audio_server.cpp);
the MP3 itself was not the cause.

The final exit flow now stops active players and queued voice work, then waits
at a bounded reference-count fence before quitting. Save failure still cancels
before audio halt. The focused helper passed 20/20, and the generalized native
process-start/exit checks passed 10/10 with zero warnings or errors. The full
native capture retained 33 images, 120 input checks and all music assertions;
the final code changes affected exit coverage and timeout handling only.
Hosted final exact-head validation remains pending, as do repeated packaged
Windows runs. Results in this section come from distinct validation revisions
and are not all claims against one SHA.

The performance measurement pass is complete and separate from that hosted
validation. It used the actual copied private aged 123-year, 100-system save,
an RTX 3080 Ti with the OpenGL compatibility renderer, five seconds per sample
after warmup, and five simulation days advanced in running tests (zero while
paused):

- 1080p: 59.3–59.9 FPS; maximum sampled p95 frame time 16.82 ms. One planet
  sample reached 68.90 ms as an isolated spike.
- 1440p: 59.5–59.9 FPS; maximum sampled p95 frame time 16.76 ms. One planet
  sample reached 63.83 ms as an isolated spike.

The 1080p run had empty stderr; the 1440p measurement was otherwise valid for
the same game code and format. These measurements do not replace hosted exact-
head validation or the packaged Windows repeat. The automated runs used
isolated user profiles and Dummy audio output; no user audio setting was
changed.
