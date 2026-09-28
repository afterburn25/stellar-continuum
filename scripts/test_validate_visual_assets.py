#!/usr/bin/env python3
"""Regression checks for assets that previously bypassed the visual contract gate."""

import contextlib
import io
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

import validate_visual_assets as validator


class VisualContractTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        root = Path(self.directory.name)
        paths = {}
        for name, value in vars(validator).items():
            if isinstance(value, Path) and value.is_relative_to(validator.ROOT):
                paths[name] = root / value.relative_to(validator.ROOT)
                if value.is_file():
                    paths[name].parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(value, paths[name])
        shutil.copytree(validator.ICON_ROOT, paths["ICON_ROOT"])
        shutil.copytree(validator.SEMANTIC_NAV_ROOT, paths["SEMANTIC_NAV_ROOT"])
        patcher = patch.multiple(validator, **paths)
        patcher.start()
        self.addCleanup(patcher.stop)

    def validate(self):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return validator.main()

    def replace(self, path, old, new):
        original = path.read_text(encoding="utf-8")
        self.assertIn(old, original)
        path.write_text(original.replace(old, new), encoding="utf-8")

    def assert_rejected(self):
        with self.assertRaises(SystemExit) as result:
            self.validate()
        self.assertEqual(result.exception.code, 1)

    def test_committed_assets_pass(self):
        self.assertEqual(self.validate(), 0)

    def test_menu_heading_readability_and_shared_styling_rejected_on_drift(self):
        for path, old, new in (
            (validator.MAIN_MENU_LAYER, 'VisualUi.Text("STELLAR", 48)',
             'VisualUi.Text("STELLAR", 14)'),
            (validator.MAIN_MENU_LAYER, 'VisualUi.Text("C O N T I N U U M", 23)',
             'VisualUi.Text("C O N T I N U U M", 10)'),
            (validator.MAIN_MENU_LAYER, 'VisualUi.Text(_main.UiBuildLabel, 10, VisualUi.Muted)',
             'VisualUi.Text(_main.UiBuildLabel, 10, VisualUi.Accent)'),
            (validator.MAIN_MENU_LAYER, 'button.Alignment = HorizontalAlignment.Left;',
             'button.Alignment = HorizontalAlignment.Center;'),
            (validator.VISUAL_UI, 'label.AddThemeFontSizeOverride("font_size", size);',
             'label.AddThemeFontSizeOverride("font_size", 14);'),
            (validator.VISUAL_UI, 'label.AddThemeColorOverride("font_color", color.Value);',
             'label.AddThemeColorOverride("font_color", Colors.White);'),
        ):
            with self.subTest(contract=old):
                original = path.read_text(encoding="utf-8")
                try:
                    self.replace(path, old, new)
                    self.assert_rejected()
                finally:
                    path.write_text(original, encoding="utf-8")

    def test_svg_style_and_reference_bypasses_rejected(self):
        path = validator.ICON_ROOT / "core/icon_hud_pause.svg"
        original = path.read_text(encoding="utf-8")
        for addition in (
            '<style>path { stroke: red }</style>',
            '<path d="M4 4h8" style="stroke: red"/>',
            '<path d="M4 4h8" onload="alert(1)"/>',
            '<use href="../other.svg#symbol"/>',
            '<path d="M4 4h8" href="file:///tmp/other.svg"/>',
            '<path d="M4 4h8" stroke-width="5"/>',
            '<path d="M4 4h8" stroke-linejoin="miter"/>',
            '<path xmlns="urn:other" d="M4 4h8"/>',
        ):
            with self.subTest(addition=addition):
                path.write_text(original.replace("</svg>", addition + "</svg>"), encoding="utf-8")
                self.assert_rejected()

    def test_svg_root_caps_and_fill_rejected(self):
        path = validator.ICON_ROOT / "core/icon_hud_pause.svg"
        original = path.read_text(encoding="utf-8")
        for old, new in (("stroke-linecap=\"round\"", "stroke-linecap=\"square\""),
                         ("fill=\"none\"", "fill=\"#E6F0F6\"")):
            with self.subTest(attribute=old):
                path.write_text(original.replace(old, new), encoding="utf-8")
                self.assert_rejected()

    def test_unregistered_family_rejected(self):
        path = validator.ICON_ROOT / "unregistered/icon_extra.svg"
        path.parent.mkdir()
        shutil.copyfile(validator.ICON_ROOT / "core/icon_hud_pause.svg", path)
        self.assert_rejected()

    def test_missing_semantic_asset_rejected(self):
        (validator.SEMANTIC_NAV_ROOT / "nav_research.svg").unlink()
        self.assert_rejected()

    def test_semantic_external_reference_rejected(self):
        path = validator.SEMANTIC_NAV_ROOT / "nav_relations.svg"
        original = path.read_text(encoding="utf-8")
        path.write_text(original.replace("</svg>", '<use href="https://example.invalid/icon.svg#x"/></svg>'), encoding="utf-8")
        self.assert_rejected()

    def test_semantic_paint_requires_one_existing_internal_gradient(self):
        path = validator.SEMANTIC_NAV_ROOT / "nav_settings.svg"
        original = path.read_text(encoding="utf-8")
        for paint in ("url(#missing)", "url(#metal) url(https://example.invalid/paint)"):
            with self.subTest(paint=paint):
                path.write_text(original.replace("url(#metal)", paint), encoding="utf-8")
                self.assert_rejected()

    def test_palette_value_drift_rejected(self):
        self.replace(validator.RUNTIME_PALETTE, "Rgb(0x05, 0x0B, 0x12)", "Rgb(0x06, 0x0B, 0x12)")
        self.assert_rejected()

    def test_theme_color_alpha_and_focus_binding_drift_rejected(self):
        original = validator.THEME.read_text(encoding="utf-8")
        for old, new in (
            ("Color(0.027451, 0.07451, 0.121569, 0.96)", "Color(0.9, 0.9, 0.9, 0.96)"),
            ("Color(0.027451, 0.07451, 0.121569, 0.96)", "Color(0.027451, 0.07451, 0.121569, 0.1)"),
            ('Button/styles/focus = SubResource("StyleBoxFlat_button_focus")',
             'Button/styles/focus = SubResource("StyleBoxFlat_button_normal")'),
            ("draw_center = false", "draw_center = true"),
        ):
            with self.subTest(property=old):
                validator.THEME.write_text(original.replace(old, new), encoding="utf-8")
                self.assert_rejected()

    def test_missing_runtime_icon_rejected(self):
        self.replace(validator.ICON_LIBRARY, "core/icon_hud_pause.svg", "core/missing.svg")
        self.assert_rejected()

    def test_readability_loss_rejected_even_when_values_agree(self):
        # Change all three representations together: parity alone must not pass.
        self.replace(validator.TOKENS, '"#6F8494"', '"#526574"')
        self.replace(validator.RUNTIME_PALETTE, "Rgb(0x6F, 0x84, 0x94)", "Rgb(0x52, 0x65, 0x74)")
        self.replace(validator.THEME, "Color(0.435294, 0.517647, 0.580392, 1)",
                     "Color(0.321569, 0.396078, 0.454902, 1)")
        self.assert_rejected()


if __name__ == "__main__":
    unittest.main()
