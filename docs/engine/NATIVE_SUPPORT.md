# Native diagnostic export

F8 or EXPORT DIAGNOSTICS in the campaign pause menu creates a local ZIP beside
the campaign save, under `support/bundle-<unique-id>/support.zip`. The menu shows
the completed path or a readable failure; neither result closes the campaign.
Settings, text entry and stronger confirmation dialogs retain keyboard input.
Exporting does not save, advance or alter the campaign and never uploads data.

## Ownership and limits

`NativeSupportService` owns one background writer and an immutable request.
Busy requests are ignored rather than queued; failures require a new explicit
request. Main-thread polling is nonblocking. The worker receives no Core or UI
objects. Its result is applied on the main thread. Shutdown joins a pending
worker; operating-system file I/O is not cancellable.

The three fixed archive entries are `session.log`, `system.txt` and, when it
exists, `campaign.player17.json`. The last completed save is included verbatim;
the export never requests a newer save. Missing saves are allowed and recorded
as `SaveIncluded=no`; nonregular, unreadable and oversized saves fail clearly.
The saved campaign may contain full authoritative information and is intended
for local troubleshooting, not an observer-filtered player information panel.

The session log retains at most 128 lines, each with a 48-byte category and
1024-byte message. It records activation, changed session notices, accepted
player reports and export outcomes. It is not a crash dump or comprehensive
tactical trace. Text inputs are capped at 256 KiB each, save input at 64 MiB
and total input at 65 MiB. The archive is streamed without a second ZIP-sized
buffer. Construction of the service performs no filesystem writes.

Each request exclusively allocates a fresh directory with a bounded collision
retry count. It writes and closes `support.zip.partial`, then renames within
that directory. On failure only its own partial file is removed. Existing
archives and the source save are untouched; an empty allocated directory can
remain after failure. The exporter checks reads, writes, flush, close and
publication. UTC log timestamps and nondeterministic filenames are diagnostic
metadata, never simulation inputs.

Metadata records native version, actual renderer backend and presentation mode,
viewport, system count, simulation day and save policy. It does not fabricate
a GPU model or detected monitor count. Core, NativeCampaignSession and Player17
are unchanged.

## Incoming review

This selectively adapts Devin `22cff368`. The ZIP layout and support action are
retained in a bounded standalone utility. Eager constructor disk writes,
synchronous UI-thread export, second-resolution overwrite destinations and
uncaught filesystem failures are replaced. The incoming notification source
link fix is unnecessary here because our feed remains client-owned.

Devin `31a28e3c` candidate shortcuts were also reviewed, but not imported. They
cycle candidates by list index and directly start work, with a smoke marker
that only proves a status notice. Existing research/production UI uses selected
quoted actions; any shortcut adaptation needs stable candidate selection and
successful authoritative action proof, not just a denial/status message.

## Validation

MSVC native client build and four focused CTests pass: `native_support`,
`native_support_service`, `native_ui_layout`, `native_client_input`.
The utility tests cover ZIP/CRC round trips, Unicode paths, repeated exports,
missing/nonregular/oversized saves, invalid destinations and source preservation.
Service tests cover immutable snapshots, bounded history, busy rejection,
nonblocking polling, caught worker failures and explicit recovery without retry.

`validate_native_support_export` is wired into the maintained Windows export.
It launches the relocated Vulkan client at 720p and 1080p with a restricted PATH.
The first launch exercises the actual pause-menu action, same-frame repeated
presses and F8, producing exactly two unique ZIPs. The second loads the saved
campaign with its support destination deliberately blocked by a regular file.
Both prove settings ownership, paused day and full canonical payload equality,
and byte-for-byte save preservation during export. Both ZIPs pass independent
Python CRC, entry, metadata and exact saved-byte checks. The forced failure
leaves the blocker intact, creates no archive/partial and exits normally after
the test, with a visible error and the campaign still open.

Six Python falsification tests reject malformed/duplicate/mistyped/false proof,
bad paths, corrupt ZIP contents, unexpected entries, incorrect metadata/save
bytes and missing save timestamps. Two neighboring native navigation launches
also pass fresh/reload and keyboard/modal contracts. Final success and failure
menu captures were visually inspected at 720p and 1080p.

Evidence: `work/native-support-build.log`, `work/native-support-focused.log`,
`work/native-support-python.log`, `work/native-support-runtime.json` and
`work/native-support-navigation-runtime.json`. Captures and two preserved ZIPs
are beside `work/native-audio-validation/package`. These are isolated native
input-event replays, not physical mouse/keyboard injection. This local package
is unsealed; this result does not establish full parity, a release download,
clean-machine compatibility or sustained 60 FPS.
