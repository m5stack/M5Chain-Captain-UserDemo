# Assets

Static UI resources are declared in `manifest.json`. Source files stay in
`images/` and `fonts/`; generated C descriptors are written to `res_c/`.
`assets.h` and `assets.cmake` are generated as part of the same operation.

Generated files are committed so a normal simulator or firmware build does not
need the asset conversion toolchain.

This is the canonical asset tree for the GitHub project. The simulator's
`simu/assets/` path links here, so maintain shared resources in `main/assets/`.

## Generate

Create a project-local Python environment and install the pinned image
conversion dependencies:

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r assets/requirements.txt
```

Font generation additionally requires the pinned `lv_font_conv` release in
`PATH`, or the `LV_FONT_CONV` environment variable pointing to it:

```bash
npm install --global lv_font_conv@1.5.3
```

From the `main/` directory, generate or verify all declared assets:

```bash
.venv/bin/python assets/generate_assets.py
.venv/bin/python assets/generate_assets.py --check
```

The CMake targets `generate_assets` and `check_assets` use
`CHAIN_CAPTAIN_ASSET_PYTHON`, which defaults to the Python interpreter found by
CMake. Set it to the same `.venv/bin/python` when configuring the project.
Generation is intentionally not part of the default build.

## Manifest

The `format` prefix selects the generated descriptor and its pixel layout.
Material icons rendered by LVGL should normally use `LVGL_A8`, which preserves
antialiasing and can be recolored. SVG entries require an explicit output size:

```json
{
    "source": "images/navigation/arrow_back.svg",
    "symbol": "image_navigation_arrow_back",
    "format": "LVGL_A8",
    "width": 24,
    "height": 24
}
```

Use `LVGL_RGB565A8` for colored LVGL images with transparency and
`LVGL_RGB565` for opaque LVGL images. Opaque images consumed only by M5GFX can
use `GFX_RGB565_SWAPPED`; this generates a lightweight GFX descriptor whose
pixels can be copied directly into the display-order RGB565 canvas. Raster
input can also set `width` and `height` to request resizing:

```json
{
    "source": "images/apps/settings.png",
    "symbol": "image_app_settings",
    "format": "GFX_RGB565_SWAPPED",
    "width": 100,
    "height": 100
}
```

Noto Sans fonts should contain only the glyphs needed by the UI. English ASCII
plus selected unit symbols can be declared as follows:

```json
{
    "source": "fonts/NotoSans/NotoSans-SemiBold.ttf",
    "symbol": "font_noto_sans_semibold_16",
    "size": 16,
    "bpp": 4,
    "ranges": ["0x20-0x7E"],
    "symbols": "\u00b0\u00b5",
    "args": ["--no-compress", "--no-prefilter", "--force-fast-kern-format"]
}
```

Keep the upstream license alongside downloaded Material and Noto source files.
