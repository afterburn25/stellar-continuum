# Immersive texture provenance

Downloaded 2026-09-10 for the 3D solar-system and surface presentation. All files are bounded JPEG assets (about 15.3 MB total); no existing `earth.jpg` or `moon.jpg` files were overwritten.

| Asset | Dimensions / format | Source download URL | Source / license | SHA-256 |
|---|---:|---|---|---|
| `assets/visual/sol/earth-map.jpg` | 5400×2700, JPEG RGB | https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/bmng-topography-bathymetry/january/world.topo.bathy.200401.3x5400x2700.jpg | NASA Earth Observatory, Blue Marble: Next Generation January topography/bathymetry map. NASA government imagery; public domain courtesy credit. Source page: https://science.nasa.gov/earth/earth-observatory/blue-marble-next-generation/base-map/ | `1684c4f8f51970dcb4a7451302bf3be17bed657aed9fece6f80d7b191e8afa3d` |
| `assets/visual/sol/moon-map.jpg` | 2048×1024, JPEG RGB | https://svs.gsfc.nasa.gov/vis/a000000/a004700/a004720/lroc_color_2k.jpg | NASA Scientific Visualization Studio CGI Moon Kit, LROC color map. NASA government imagery; public domain courtesy credit. Source page: https://svs.gsfc.nasa.gov/4720/ | `f7130a1822681fa7512d7dcfd40db8c10b9ba4f06777910348698260ed7a2170` |
| `assets/visual/sol/earth-clouds.jpg` | 2048×1024, JPEG RGB | https://eoimages.gsfc.nasa.gov/images/imagerecords/57000/57747/cloud_combined_2048.jpg | NASA Blue Marble cloud layer by Reto Stöckli. NASA government imagery; public domain courtesy credit. Source page: https://visibleearth.nasa.gov/images/57747/blue-marble-clouds | `daddaad84d7a33bbbc86cdda3f591099f57cee8607b7bcf3b67eb7e4f7a1c793` |
| `assets/visual/terrain/grass-ground-albedo.jpg` | 2048×2048, JPEG RGB | https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/grass_ground/grass_ground_diff_2k.jpg | Poly Haven “Grass Ground”, Diffuse map, by Charlotte Baglioni. CC0. Asset page: https://polyhaven.com/a/grass_ground; license: https://polyhaven.com/license | `4272bddc71a7fa659d1deeb13a9df8c42e68a5ac8cde7523187e4a081e028258` |
| `assets/visual/terrain/grass-ground-normal.jpg` | 2048×2048, JPEG RGB | https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/grass_ground/grass_ground_nor_gl_2k.jpg | Poly Haven “Grass Ground”, OpenGL Normal map, by Charlotte Baglioni. CC0. Asset page: https://polyhaven.com/a/grass_ground; license: https://polyhaven.com/license | `effa95c06af273c974476d9a74a234668a1c9292f60fffb4fa40348d638ee7f3` |
| `assets/visual/terrain/grass-ground-roughness.jpg` | 2048×2048, JPEG 8-bit grayscale | https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/grass_ground/grass_ground_rough_2k.jpg | Poly Haven “Grass Ground”, Rough map, by Charlotte Baglioni. CC0. Asset page: https://polyhaven.com/a/grass_ground; license: https://polyhaven.com/license | `afaed95537e894b8054db38c81712a6d9504b05b560274526621d91d5a95fbe7` |

## Transformations

The downloaded NASA and Poly Haven files were copied byte-for-byte to the filenames above. No resizing, color conversion, channel inversion, or TIFF-to-JPEG conversion was needed. The Earth map is an equirectangular 5400×2700 image; the Moon map is NASA’s 2K equirectangular color image. The Poly Haven normal map is the supplied OpenGL (`nor_gl`) variant.

## Earth night lighting addition

`assets/visual/sol/earth-night-map.jpg`: 3600×1800 JPEG, 779,638 bytes, copied byte-for-byte from https://assets.science.nasa.gov/content/dam/science/esd/eo/images/imagerecords/144000/144898/BlackMarble_2016_01deg.jpg . NASA Earth Observatory Black Marble 2016; source page https://science.nasa.gov/earth/earth-observatory/earth-at-night/maps/ . NASA government imagery; courtesy credit to NASA Earth Observatory. SHA-256: `d87de751a264e4f8ff69c68de5dab9606daee87a6f15ae743c93200743bd7ec1`. Imported with mipmaps. The shader rejects the faint base land color and lights only the night hemisphere of the owner's inhabited Earth.
