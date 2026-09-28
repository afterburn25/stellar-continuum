# Diplomacy workspace

The implemented `DiplomacyWorkspaceView` is a full Relations workspace built on the existing
observer-filtered `DiplomaticStateView`. `DiplomacyWorkspacePresenter` shapes that view into a
pure model; the Godot view owns stable nodes, callbacks, and rendering.

The screen contains a searchable/filterable contact rail, selected-contact transmission and
identity area, political status and communication state, five relationship meters, directional
transit access, active agreements, pending incoming/outgoing proposals, recent diplomatic
history, and context-sensitive action controls. Unknown contacts remain generic. Unknown meter
values remain unknown rather than zero. Stale/lost contacts cannot appear as available channels.
Filters cover all, identified, unidentified, cooperative, neutral, hostile, at war, pending
proposal, and communication available.

Proposal, agreement, access, ceasefire, peace, and war actions dispatch through
`ObserverDiplomacyCommandService` and its existing availability contracts. The workspace does not
duplicate legality rules or receive authoritative hidden state. Per-contact pending counts,
source indices, observer-visible confidence, and known political state are preserved in the DTO.
Hidden names are never requested for unidentified contacts.

Diplomatic history is published through the existing player notification feed. Voice presentation
uses the observer-safe voice bridge/router, and future Galactic News Network consumption must use
only observer-visible events. Voice and news remain optional and cannot expose private negotiations.

Reusable panels and scroll containers reflow at 720p without shrinking essential text, while the
same model supports 1080p and larger layouts. Four panoramic communications-room scenes fill
the transmission area, with the complete representative at a species-appropriate console.
`VisualIconLibrary` exposes original color-coded
semantic SVGs for research, economy, construction, shipyard, exploration, colonization, logistics,
relations, inspection, home, galaxy, and settings.

The 3:1 scenes preserve aspect ratio and crop only extended side scenery at the supported
16:9 layouts. There are no inset portraits or empty sidebars. Channel state sits above the
image; names and subtitles occupy their own panel below. At 720p the lower details area
scrolls to reserve picture height even during two-line dialogue; longer subtitles have an
ellipsis and full-text tooltip. Mipmapped linear sampling avoids sparkling fine detail when
the scene is reduced. Square identity portraits remain available for other screens.
See `art/DIPLOMACY_COMMUNICATIONS_PROVENANCE.md` for asset paths, generation tool and prompts.

Detailed species/leader art, richer negotiation terms, full Galactic News Network presentation,
and native voice styling remain extension seams over existing systems. Native GUI acceptance,
Windows packaging, and final visual approval require current evidence.

Validation requires a game build, focused diplomacy validation, and maintained native capture at
the exact source revision. Review unknown contacts and hidden third-party agreements, directional
access, proposal flags, stale communication, responsive 720p/1080p layout, full alien framing,
and notifications. Source `5670d4b80204541d516d73e835124866bb7ecee0` completed both current
native receipts: `work/diplomacy-communications-5670d4b8-1280x720` and
`work/diplomacy-communications-5670d4b8-1920x1080`, each with exit 0, empty stderr,
23 images and 119 checks. Debug and Release builds have zero warnings/errors; the five
focused diplomacy checks and visual-asset validator passed. The 23 images comprise 19 flow
screens plus four species/long-caption fixtures. These check the actual visible texture crop,
including the entire image height and representative region, not just container bounds.
The capture uses the
Dummy audio driver, so it establishes no audibility claim. The CI X11 allowlist accepts only the
documented unsupported-V-Sync warning while retaining raw stderr and failing other runtime errors.
Earlier source `e3522ba6` passed Windows packaging/runtime, build, voice and research CI.
Its screenshot run `34641285236` passed both native diplomacy captures, then timed out
(exit 124) in the broader game capture after 600 seconds; final screenshot validation did
not run. New-source CI remains pending. PR #313 stays draft; no release/merge claim is made.
The failed local caption checks are retained beside the passing receipts; they revealed
and led to fixing a three-pixel image crop at 720p with wrapped dialogue.
