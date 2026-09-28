# Native celestial detail and artwork review

## Retained artwork and improved detail

The native client retains the approved nine Sol photographs/maps declared by
`export/native-celestial-assets.json`, including the restored Earth image. Source
discs now use 512x512 RGBA pixels instead of 256x256. Surveyed procedural bodies
use 256x256 instead of 96x96. These are rendering resolutions, not claims that
upscaling adds information to a low-resolution source. Existing cropping, colour,
lighting and transparent limb coverage remain unchanged.

The disc cache still owns at most 96 entries and 16 MiB. The shared asynchronous
preparation queue reserves the actual output size: 1 MiB per source-backed disc
or 256 KiB per procedural disc. Saturation remains retryable; requests deduplicate,
and cancelled campaign generations cannot publish old images. Pan/zoom reuses the
same immutable images. Undiscovered bodies cannot acquire a known surface.

Tests exercise both output sizes, transparent edges, approved photographic colours,
background/synchronous equality, deduplication, generation cancellation, backpressure
and byte-based eviction. A source-only pressure test retains exactly sixteen 1 MiB
discs within the original budget. This remains a 2D orbital presentation; it does
not complete atmospheric descent, 3D terrain or physically shaded planets.

## Pending upstream artwork

Reviewed Devin `bc681eb6`, `fe364430` and `aabde2b1`; not imported wholesale:

- `bc681eb6` replaces the approved Sol photographs with generated PNGs, including
  Earth. The requested restored Earth appearance must remain. Its seeded ring
  choices also invent planetary properties in presentation instead of following
  observer-filtered authoritative body data.
- The G/M star images inspected at `aabde2b1` show abrupt inner-disc/outer-field
  transitions. These need compositing review at actual map and close-focus sizes
  before replacing the current photospheres; image resolution alone is insufficient.
- The sprite loader introduced in `fe364430` decodes synchronously into a separate
  map. Eleven 1024x1024 RGBA sprites add about 44 MiB outside the maintained 32 MiB
  celestial cache and its statistics; that map is not cleared by the existing
  cache-clear operation. Import must use the established asynchronous queue and
  bounded ownership instead.
- Reproduction notes reference machine-local generation tools and general source
  sites. A maintained asset import should identify its exact sources, transformation
  steps, packaged attribution and content hashes. Repository wording calling art
  approved does not establish user approval of replacing the existing Earth.

Future imports should demonstrate alpha-correct edges without rectangular frames,
stable class colours, observer-safe selection, bounded loading, package integrity
and comparative screenshots at 720p/1080p before adoption.

## Capture geometry contract

Each successful native BMP readback emits a `native_capture` JSON record containing
the absolute UTF-8 path and actual readback width/height. The final smoke summary
also reports the drawable size for diagnostics. Readback records are emitted before
their enclosing inspection results, keeping each structured record intact.

The shared export validator binds a BMP to its own unique capture record. Startup,
settings-preview and final frames may legitimately differ, so a process-wide size
or accepting either logical or drawable dimensions is insufficient. When records
exist, missing/duplicate/conflicting or malformed evidence fails. Older captures
without any records must match the requested dimensions exactly.

Validation checks headers, file/pixel bounds, bitfield masks, dimensions and real
RGB variation; alpha or row-padding changes alone do not count as a rendered frame.
New Game, galaxy, colony, settlement, surface and system validators share this
contract. CPU work stops at the first colour difference instead of accumulating
every colour in a large screenshot. Graphical replay still uses application input
routing; this contract does not establish physical-input or sustained-FPS parity.
