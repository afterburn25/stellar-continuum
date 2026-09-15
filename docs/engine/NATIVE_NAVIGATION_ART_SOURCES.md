Native navigation icons use the four reviewed SVGs already present in
`assets/visual/ui/navigation/`: research, shipyard, construction and relations.
The checked-in 256px transparent PNGs are deterministic derivatives generated
with the pinned development-only `resvg_py==0.5.0` wheel. The wheel is not a
runtime or shipped build dependency; regenerate with
`tools/stellar-export/regenerate_navigation_art.py` only when reviewing a
source change, then update the exact hashes in
`export/native-navigation-assets.json`. Normal builds decode the checked-in
PNGs with the existing Engine image path; they do not install Python packages.

Renderer source: [resvg-py](https://github.com/baseplate-admin/resvg-py),
[pinned package](https://pypi.org/project/resvg_py/0.5.0/).
The artwork remains the repository's existing approved icon designs. No new
third-party artwork is introduced by this conversion.

To reproduce in PowerShell from the repository root:

```powershell
python -m pip install --only-binary=:all: --no-deps --target work/navigation-art-tools resvg_py==0.5.0
python -c "import runpy, sys; sys.path.insert(0, 'work/navigation-art-tools'); runpy.run_path('tools/stellar-export/regenerate_navigation_art.py', run_name='__main__')"
python tools/stellar-export/test_native_navigation_assets.py
```

The script checks the tool version and regenerates four 256 x 256 transparent
images. The final command verifies them against the reviewed hashes; it does
not approve or automatically update a changed hash. The four decoded images
use a total of 1 MiB and are reused for every frame.
