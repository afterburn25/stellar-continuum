# Premium diplomacy handoff

The branch contains the integrated observer-safe Relations workspace. Pure shaping is exposed by
`DiplomacyWorkspacePresenter.Build(DiplomaticStateView, ...)`; `DiplomacyWorkspaceView` stores the
model and exposes stable node names plus callbacks for contact, proposal, and action integration.
Rendering is supplied by the workspace render partial and Main integration.

Implemented sections are the contact/search/filter rail, selected civilization presentation,
communication state, political status, Trust/Hostility/Fear/Respect/Cooperation meters, directional
access, agreements, incoming/outgoing proposals, recent history, and action bar. Filters are
observer-safe and per-contact. Unknown names and values stay unknown; stale/lost channels are
unavailable. Source contact indices survive filtering. The current alien framing keeps the entire
source figure visible inside a panoramic communications room, at a species-appropriate
terminal. The room fills the picture area, extended side scenery absorbs responsive cropping,
and captions remain below the image. Original square identity images remain untouched.

Commands remain authoritative in `ObserverDiplomacyCommandService`. Notifications use the existing
player feed; voice uses the observer-safe voice bridge/router; future Galactic News Network
integration must consume only known diplomatic events. Semantic navigation icons are available
through `VisualIconLibrary` under `assets/visual/ui/navigation/`; `CampaignSidebar` remains the
mapping owner.

CPU validation covers anonymous contacts, hidden relationships, proposal direction,
communication/proposal/agreement/war/ceasefire/peace lifecycles, directional access, history, and
hidden unrelated observer state. Runtime source `5670d4b80204541d516d73e835124866bb7ecee0`
completed both current native captures: `work/diplomacy-communications-5670d4b8-1280x720`
and `work/diplomacy-communications-5670d4b8-1920x1080`, each exit 0, empty stderr,
23 images and 119 checks (19 flow screens plus four art/long-caption fixtures).
Debug/Release build 0 warnings/errors, focused diplomacy 5/5 and asset validation passed.
Scenes and full built-in generation prompts are recorded in
`docs/art/DIPLOMACY_COMMUNICATIONS_PROVENANCE.md`. Linear mipmaps improve reduced-image
clarity; 720p scrollable details reserve full picture height during wrapped dialogue.
Audio used Dummy, so no audibility claim is made. CI keeps
raw stderr and allowlists only the documented unsupported-V-Sync X11 warning; other runtime/.NET
errors remain fatal. Earlier source `e3522ba6` passed Windows/build/voice/research CI.
Screenshot run `34641285236` passed diplomacy, then timed out in the generic game capture
at 600 seconds (exit 124); screenshot validation was skipped. Current source awaits CI.
PR #313 stays draft against integration; these receipts are not release approval.

Extension seams include richer species/leader presentation, negotiation terms, voice styling, and
Galactic News Network display. No hidden AI state or unsupported treaty concepts belong in those
seams.
