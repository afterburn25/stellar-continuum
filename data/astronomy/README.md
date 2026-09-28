# Nearby star catalog

The full-galaxy game profile retains the nearest 96 classified systems from this file at
their measured positions, then generates fictional systems around them. Its compact radius
scales with the 250/500/1,000/2,500 population to keep useful interstellar spacing; this does
not represent the physical size of the real Milky Way. Generated names and positions do not
claim catalogue provenance. Existing nearby-only saves retain the original 500-source profile.

`hyg-nearby-500-v1.json` is a reproducible selection of the 500 nearest valid HYG systems, grouped by HYG `comp_primary`. Multiple components of a system are visited once; widely separated catalog systems such as Proxima Centauri remain separate where HYG assigns a separate primary.

The source is HYG Database v4.1 at the pinned commit [c7f7f883fe678cc7680169a50ccd7dcc49b060ce](https://github.com/astronexus/HYG-Database/tree/c7f7f883fe678cc7680169a50ccd7dcc49b060ce), downloaded from the URL recorded in the JSON. HYG is licensed CC BY-SA 4.0 ([license](https://creativecommons.org/licenses/by-sa/4.0/)); this derived JSON is provided under the same license with attribution. The source SHA-256 is recorded in the JSON and can be reproduced with `scripts/build_nearby_star_catalog.py --source <hygdata_v41.csv> --verify`.

This is not a complete census and is not a current Gaia release. Distances are the catalog's approximate parallax-derived values; no estimates are substituted for missing values. Coordinates are HYG equatorial J2000 XYZ converted from parsecs to light-years (3.26156). The Sun is exactly at (0, 0, 0). Planet content elsewhere in the game is fictional.

Reference distances in this catalog are approximately Barnard's Star 5.95 ly, Sirius 8.60 ly, Rigil Kentaurus (Alpha Centauri A/B) 4.32 ly, and Proxima Centauri 4.23 ly. These preserve this pinned catalogue's parallax values, not newer measurements. The selected range ends at 40.7186 light-years. The JSON contains 500 systems: 50 proper names and 450 catalogue designations. Case-insensitive duplicate names are disambiguated by HYG id.

The source XYZ direction is normalized to the source's recorded parallax distance to eliminate rounding discrepancies, and Sol is placed at exactly zero. Neither the seed nor artwork fitting moves these stellar positions. This is a projection of a local three-dimensional sample, not a reconstruction of the entire Milky Way. Systems without a recorded spectral class retain an unknown classification rather than being assigned an invented measured type. HYG's component grouping is incomplete; the game displays up to three recorded components.
