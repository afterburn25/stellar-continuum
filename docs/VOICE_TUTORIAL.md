# Officer-led voice tutorial

Open **Guide → Start / resume voice tutorial** during a campaign. **Restart tutorial** starts the lessons again without creating or resetting a campaign.

Thirteen player-paced lessons cover the opening: map navigation, the clock, economy, construction, research, live First Light objectives, ships, reconnaissance, surveys, colonies, diplomacy and saving. Current officers resolve through the existing character/role/species voice system. Tutorial dialogue uses the installed offline voice provider and existing caption settings.

Starting pauses the simulation. The player can resume it with the normal controls; changing or closing lessons never silently resumes time. Each lesson has written instructions, a full transcript, an explicit screen-navigation button, Back, Next lesson, Replay, Stop voice and Close. There are no mandatory waits or forced game orders. Finishing the tutorial records learning progress only, without granting research, resources, ships, surveys or colonies.

The last lesson ID and completion preference are saved atomically to `user://voice-tutorial-progress.json`, independently of campaign saves. Reopening the game does not automatically start speech. Closing retains the lesson; campaign replacement closes the active guide. Opening the campaign menu, Developer tools or a planet surface suspends tutorial narration. Return to the guide and Replay when ready.

Tutorial cancellation affects only its own category. Critical announcements remain in the shared voice system. Voice mute and missing providers retain written lessons/captions. Tutorials respect the current character roster; they do not inspect foreign private offices.

At 1280×720, the operations drawer aligns left while the tutorial occupies the right. The research workspace reserves space for the tutorial and active caption so its project inspector and action remain accessible. Normal layout returns after closing.

The tutorial is an introductory, manually advanced walkthrough. It does not yet detect whether a learner completed each suggested action, highlight individual controls, or provide a full campaign-length adaptive coach.

## Validation

`STELLAR_CAPTURE_FOCUS=voice-tutorial` runs the focused native acceptance path in `tools/ScreenshotCapture.tscn`. It exercises actual pointer input, real neural commander/scientist speech and non-silent Voice-bus captures, 720p/1080p layout, relevant-screen navigation, close/replay/mute, independent alert retention, menu suspension, completion persistence, restart, campaign replacement and unchanged paused simulation time. It remains separate from the complete release gate.
