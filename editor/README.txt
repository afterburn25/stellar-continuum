STELLAR ENGINE EDITOR 0.1.1

Double-click StellarEngineEditor.exe. Keep the folders beside it.
This Windows x64 package includes its desktop runtime and the native Stellar Engine 0.1.9.

GETTING STARTED
The First Light example opens automatically. Use Save project to save your own copy.
Generate world runs the converted native engine and creates actual stars, planets,
civilizations and starting colonies. Scroll to zoom, drag to pan, and click a star
to inspect it. Right-drag to orbit; Viewing angle changes the tilt. Expand fills
the workspace with the galaxy. Switch Galaxy to Map for clear editing markers.
The outliner also lists bodies, civilizations and colonies.
Edit a star's display name, design notes and bookmark, then click Apply to project.
Ctrl+S saves. Ctrl+Z and Ctrl+Y undo/redo project changes outside text fields.

ENGINE ASSETS
Starter game art and audio, officer tutorial clips and historical voice auditions
are imported automatically into Documents\Stellar Engine\Assets on first use.
Use the asset category filter and search box to find an item. Import keeps the
original file and copies it into the persistent engine library. Duplicate content
with the same filename is reused; different content is stored separately.
Select an asset and use Preview to view PNG/JPG images or hear supported audio.
Use Stop audio to end playback. Other supported types remain available as files;
model, SVG, OGG and FLAC preview availability is limited in this first build.
Use Add to project to include a portable copy in the saved project. Opening that
project on another installation restores its assets into that editor's library.
Voice Auditions contains earlier voice samples; the Officer Tutorial folder
contains the later tutorial cast. These are assets, not a change to the game's
active voice assignments. Existing source asset notices remain included.

PROJECTS AND OUTPUT
.stellar-project files contain generation settings, the generated world,
annotations and selected assets. Saving keeps the previous file as .bak.
Export world snapshot writes native preview data plus editor annotations.
Imported assets are limited to 25 MB each and 100 MB per project in this preview.
The undo history keeps 12 project changes. Projects are separate from game saves.

CURRENT SCOPE
This is a working first world editor, not the finished editor suite.
Galaxy view adds procedural stellar light, a central bulge, spiral arms and dust
lanes. These are illustrative; selectable systems use the native catalog. The
compact world scale is a gameplay layout, not a measured full-size Milky Way.
The view is a desktop editor feature; the game's native renderer is still planned.
Full campaign play, a scene/model editor, simulation-property editing and playable
game export require later migration/editor work. Display names and notes are
authoring annotations; they do not alter physical simulation rules.

The existing Godot game remains the playable baseline. This editor uses a pinned
validated native runtime and does not modify game saves or the migration worktree.
